#include "backend/services/ProcessingService.h"
#include "backend/services/Hdf5Service.h"
#include "backend/diagnostics/CrashStateMirror.h"
#include "backend/playback/FrameStore.h"
#include "backend/Tools.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <iterator>
#include <limits>
#include <tuple>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace backend::services {

namespace {

size_t defaultMaxBufferedFrames(size_t flushInterval) {
    constexpr size_t kMinBufferedFrames = 1000;
    constexpr size_t kSoftMaxBufferedFrames = 5000;

    const size_t scaled = flushInterval > (std::numeric_limits<size_t>::max() / 4)
                              ? std::numeric_limits<size_t>::max()
                              : flushInterval * 4;
    const size_t preferred = std::max(kMinBufferedFrames, scaled);
    return std::max(flushInterval, std::min(preferred, kSoftMaxBufferedFrames));
}

} // namespace

ProcessingService::ProcessingService() = default;
ProcessingService::~ProcessingService() { stop(); }

void ProcessingService::start(size_t workerCount) {
    if (running_.load()) return;
    running_.store(true);
    if (workerCount == 0) workerCount = 1;
    workers_.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&ProcessingService::workerLoop, this);
    }
    {
        auto& m = backend::diagnostics::CrashStateMirror::instance().processing;
        m.running.store(true);
        m.workerCount.store(static_cast<int>(workerCount));
    }
    SPDLOG_INFO("ProcessingService started with {} workers", workerCount);
}

void ProcessingService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
    // drain queue
    {
        std::scoped_lock lk(mutex_);
        std::queue<Job> empty;
        queue_.swap(empty);
    }
    {
        auto& m = backend::diagnostics::CrashStateMirror::instance().processing;
        m.running.store(false);
        m.workerCount.store(0);
    }
    SPDLOG_INFO("ProcessingService stopped");
}

void ProcessingService::submit(Job job) {
    if (!running_.load()) return;
    {
        std::scoped_lock lk(mutex_);
        queue_.push(std::move(job));
        stats_.jobsQueued.fetch_add(1, std::memory_order_relaxed);
        backend::diagnostics::CrashStateMirror::instance().processing.jobsQueued
            .store(stats_.jobsQueued.load(std::memory_order_relaxed),
                   std::memory_order_relaxed);
    }
    cv_.notify_one();
}

void ProcessingService::workerLoop() {
    while (running_.load()) {
        Job job;
        {
            std::unique_lock lk(mutex_);
            cv_.wait(lk, [&]{ return !running_.load() || !queue_.empty(); });
            if (!running_.load()) break;
            if (queue_.empty()) continue;
            job = std::move(queue_.front());
            queue_.pop();
        }
        if (job) {
            job();
            stats_.jobsProcessed.fetch_add(1, std::memory_order_relaxed);
            backend::diagnostics::CrashStateMirror::instance().processing.jobsProcessed
                .fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void ProcessingService::startRealtime(std::shared_ptr<backend::playback::FrameStore> store) {
    if (rtRunning_.load()) return;
    rtStore_ = std::move(store);
    rtRunning_.store(true);
    consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
    lastAutoBackgroundFrame_.store(0, std::memory_order_relaxed);
    {
        std::scoped_lock prevFrameLk(previousFrameMutex_);
        previousFrameForAutoCapture_.release();
    }
    realtimeThread_ = std::thread(&ProcessingService::realtimeLoop, this);
    backend::diagnostics::CrashStateMirror::instance().processing.realtimeRunning.store(true);
    SPDLOG_INFO("ProcessingService: realtime processing started");
}

void ProcessingService::stopRealtime() {
    if (!rtRunning_.load()) return;
    rtRunning_.store(false);
    if (realtimeThread_.joinable()) realtimeThread_.join();
    backend::diagnostics::CrashStateMirror::instance().processing.realtimeRunning.store(false);
    SPDLOG_INFO("ProcessingService: realtime processing stopped");
}

void ProcessingService::setRealtimeEnabled(bool on) {
    rtEnabled_.store(on);
}

void ProcessingService::setRealtimeDropFrames(bool on) {
    rtDropFrames_.store(on, std::memory_order_relaxed);
}

void ProcessingService::setRealtimeRoi(const Roi& roi) {
    std::scoped_lock lk(rtMutex_);
    rtRoi_ = roi;
}

ProcessingService::Roi ProcessingService::getRealtimeRoi() const {
    std::scoped_lock lk(rtMutex_);
    return rtRoi_;
}

void ProcessingService::setRealtimeBackgroundGray(const cv::Mat& bg) {
    std::scoped_lock lk(rtMutex_);
    if (!bg.empty() && bg.type() == CV_8UC1) {
        rtBgGray_ = std::make_shared<cv::Mat>(bg.clone());
    } else if (!bg.empty()) {
        cv::Mat tmp;
        bg.convertTo(tmp, CV_8UC1);
        rtBgGray_ = std::make_shared<cv::Mat>(std::move(tmp));
    } else {
        rtBgGray_.reset();
    }
}

cv::Mat ProcessingService::getRealtimeBackgroundGray() const {
    std::scoped_lock lk(rtMutex_);
    if (rtBgGray_ && !rtBgGray_->empty()) {
        return rtBgGray_->clone();
    }
    return cv::Mat();
}

bool ProcessingService::getLatestSnapshot(RealtimeSnapshot& out) {
    std::scoped_lock lk(snapshotMutex_);
    if (latestSnapshot_.mask.empty() && latestSnapshot_.contours.empty()) return false;
    out.index = latestSnapshot_.index;
    out.mask = latestSnapshot_.mask.clone();
    out.contours = latestSnapshot_.contours;
    out.validation = latestSnapshot_.validation;
    return true;
}

void ProcessingService::startExperiment() {
    std::scoped_lock lk(framesMutex_);
    validFrames_.clear();
    invalidFrames_.clear();
    // Reserve within the same cap used at runtime; invalid frames are sampled
    // and may dominate sparse-valid runs, but should not pre-allocate beyond
    // the bounded backlog.
    const size_t flushInterval = flushInterval_.load(std::memory_order_relaxed);
    const size_t maxBuffered = maxBufferedFrames_.load(std::memory_order_relaxed);
    const size_t validReserve = std::min(flushInterval, maxBuffered);
    validFrames_.reserve(validReserve);
    invalidFrames_.reserve(maxBuffered - validReserve);
    framesSinceLastFlush_.store(0);
    invalidFrameCounter_.store(0);
    totalValidFlushed_.store(0, std::memory_order_relaxed);
    droppedValidFrames_.store(0, std::memory_order_relaxed);
    droppedInvalidFrames_.store(0, std::memory_order_relaxed);
    lastDropLogUs_.store(0, std::memory_order_relaxed);
    resetRealtimeMetrics();
    // Reset auto-capture counter when experiment starts
    consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
    experimentActive_.store(true);
    backend::diagnostics::CrashStateMirror::instance().processing.experimentActive.store(true);
    SPDLOG_INFO("ProcessingService: experiment started, frame buffers cleared (flush interval: {} frames, max buffered: {}, invalid sampling: every {}th)",
                flushInterval, maxBuffered, invalidFrameSamplingRate_.load());
}

void ProcessingService::endExperiment() {
    experimentActive_.store(false);
    backend::diagnostics::CrashStateMirror::instance().processing.experimentActive.store(false);
    BufferedFrameCounts counts{};
    {
        std::scoped_lock lk(framesMutex_);
        counts.valid = validFrames_.size();
        counts.invalid = invalidFrames_.size();
    }
    SPDLOG_INFO("ProcessingService: experiment ended, valid frames: {}, invalid frames: {}",
                counts.valid, counts.invalid);
}

std::vector<ProcessedFrame> ProcessingService::getValidFrames() const {
    std::scoped_lock lk(framesMutex_);
    return validFrames_;
}

std::vector<ProcessedFrame> ProcessingService::getInvalidFrames() const {
    std::scoped_lock lk(framesMutex_);
    return invalidFrames_;
}

BufferedFrameCounts ProcessingService::getBufferedFrameCounts() const {
    std::scoped_lock lk(framesMutex_);
    return BufferedFrameCounts{validFrames_.size(), invalidFrames_.size()};
}

void ProcessingService::clearAccumulatedFrames() {
    std::scoped_lock lk(framesMutex_);
    validFrames_.clear();
    invalidFrames_.clear();
    framesSinceLastFlush_.store(0, std::memory_order_relaxed);
}

std::vector<ProcessedFrame> ProcessingService::getMonitoringValidFrames() const {
    std::scoped_lock lk(monitoringFramesMutex_);
    return monitoringValidFrames_.toVector();
}

std::vector<ProcessedFrame> ProcessingService::getMonitoringInvalidFrames() const {
    std::scoped_lock lk(monitoringFramesMutex_);
    return monitoringInvalidFrames_.toVector();
}

void ProcessingService::clearMonitoringFrames() {
    std::scoped_lock lk(monitoringFramesMutex_);
    monitoringValidFrames_.clear();
    monitoringInvalidFrames_.clear();
}

void ProcessingService::setProcessingConfig(const ProcessingConfig& config) {
    std::scoped_lock lk(configMutex_);
    processingConfig_ = config;
}

ProcessingConfig ProcessingService::getProcessingConfig() const {
    std::scoped_lock lk(configMutex_);
    return processingConfig_;
}

void ProcessingService::setPixelToMicronFactor(double factor) {
    pixelToMicronFactor_.store(factor, std::memory_order_relaxed);
}

double ProcessingService::getPixelToMicronFactor() const {
    return pixelToMicronFactor_.load(std::memory_order_relaxed);
}

bool ProcessingService::loadEModulusLut(const std::string& path) {
    return eModulusLut_.loadFromFile(path);
}

static inline cv::Mat makeGrayCopy(uint64_t width, uint64_t height, size_t linePitch, const uint8_t* data) {
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    const size_t step = (linePitch == 0 ? static_cast<size_t>(width) : linePitch);
    cv::Mat view(h, w, CV_8UC1, const_cast<uint8_t*>(data), step);
    return view.clone();
}

// Extract ROI directly from frame data without full frame copy
static inline cv::Mat makeGrayROI(const backend::playback::Frame& frame, int roiX, int roiY, int roiW, int roiH) {
    if (frame.data.empty() || frame.width == 0 || frame.height == 0) {
        return cv::Mat();
    }
    
    const int frameW = static_cast<int>(frame.width);
    const int frameH = static_cast<int>(frame.height);
    
    // Clamp ROI to frame bounds
    const int clampedX = std::max(0, std::min(roiX, frameW - 1));
    const int clampedY = std::max(0, std::min(roiY, frameH - 1));
    const int clampedW = std::max(1, std::min(roiW, frameW - clampedX));
    const int clampedH = std::max(1, std::min(roiH, frameH - clampedY));
    
    const size_t srcPitch = (frame.linePitch == 0 ? static_cast<size_t>(frame.width) : frame.linePitch);
    const uint8_t* srcPtr = frame.data.data() + (clampedY * srcPitch) + clampedX;
    
    // Create ROI Mat view
    cv::Mat roiView(clampedH, clampedW, CV_8UC1, const_cast<uint8_t*>(srcPtr), srcPitch);
    
    // Clone to ensure contiguous memory and ownership
    return roiView.clone();
}

bool ProcessingService::isFrameEmpty(const backend::playback::Frame& frame,
                                    const ProcessingConfig& config,
                                    const Roi& roi,
                                    const cv::Mat& background) {
    if (frame.width == 0 || frame.height == 0 || frame.data.empty()) {
        return true;
    }

    cv::Mat gray = makeGrayCopy(frame.width, frame.height, frame.linePitch, frame.data.data());

    // Determine ROI
    Roi effectiveRoi = roi;
    if (effectiveRoi.w <= 0 || effectiveRoi.h <= 0) {
        effectiveRoi.x = 0;
        effectiveRoi.y = 0;
        effectiveRoi.w = static_cast<int>(gray.cols);
        effectiveRoi.h = static_cast<int>(gray.rows);
    }
    effectiveRoi.x = std::max(0, std::min(effectiveRoi.x, gray.cols - 1));
    effectiveRoi.y = std::max(0, std::min(effectiveRoi.y, gray.rows - 1));
    effectiveRoi.w = std::max(1, std::min(effectiveRoi.w, gray.cols - effectiveRoi.x));
    effectiveRoi.h = std::max(1, std::min(effectiveRoi.h, gray.rows - effectiveRoi.y));

    cv::Rect cvRoi(effectiveRoi.x, effectiveRoi.y, effectiveRoi.w, effectiveRoi.h);
    cv::Mat roiCurr = gray(cvRoi);

    // Apply same processing as realtime loop
    cv::Mat blurredCurr, blurredBg, diff, thresh;
    cv::GaussianBlur(roiCurr, blurredCurr, cv::Size(3, 3), 0);
    
    if (!background.empty() && background.size() == gray.size() && background.type() == CV_8UC1) {
        cv::GaussianBlur(background(cvRoi), blurredBg, cv::Size(3, 3), 0);
        cv::subtract(blurredCurr, blurredBg, diff);
    } else {
        diff = blurredCurr;
    }
    
    cv::threshold(diff, thresh, config.bg_subtract_threshold, 255, cv::THRESH_BINARY);
    
    // Count non-zero pixels
    int pixelCount = cv::countNonZero(thresh);
    return pixelCount < config.empty_frame_pixel_threshold;
}

ProcessedFrame ProcessingService::computeProcessedFrame(
    const cv::Mat& grayInput,
    const cv::Mat& backgroundGray,
    const ProcessingConfig& config,
    const Roi& roiIn,
    uint64_t index,
    uint64_t timestampNs) {

    ProcessedFrame out;
    out.index = index;
    out.timestampNs = timestampNs;

    if (grayInput.empty()) {
        SPDLOG_WARN("computeProcessedFrame: empty input image");
        return out;
    }

    // Coerce input to CV_8UC1
    cv::Mat gray;
    if (grayInput.type() == CV_8UC1) {
        gray = grayInput.clone();
    } else if (grayInput.channels() == 3) {
        cv::cvtColor(grayInput, gray, cv::COLOR_BGR2GRAY);
    } else {
        grayInput.convertTo(gray, CV_8UC1);
    }
    out.originalImage = gray; // clone already via above paths

    // Clamp ROI (or default to full frame)
    Roi roi = roiIn;
    if (roi.w <= 0 || roi.h <= 0) {
        roi.x = 0;
        roi.y = 0;
        roi.w = gray.cols;
        roi.h = gray.rows;
    }
    roi.x = std::max(0, std::min(roi.x, gray.cols - 1));
    roi.y = std::max(0, std::min(roi.y, gray.rows - 1));
    roi.w = std::max(1, std::min(roi.w, gray.cols - roi.x));
    roi.h = std::max(1, std::min(roi.h, gray.rows - roi.y));
    const cv::Rect cvRoi(roi.x, roi.y, roi.w, roi.h);

    // Kernel sizing (same rules as realtimeLoop)
    auto toOdd = [](int v) -> int { if (v < 1) v = 1; if ((v % 2) == 0) v += 1; return v; };
    const int blurK = toOdd(config.gaussian_blur_size);
    const int morphK = toOdd(config.morph_kernel_size);
    const int morphIter = std::max(1, config.morph_iterations);
    const int threshVal = std::max(0, config.bg_subtract_threshold);

    // Full-size mask; process inside ROI only
    cv::Mat mask(gray.rows, gray.cols, CV_8UC1, cv::Scalar(0));
    cv::Mat roiCurr = gray(cvRoi);
    cv::Mat roiDst = mask(cvRoi);

    cv::Mat blurredCurr, diffForProcessing, thresh;
    cv::GaussianBlur(roiCurr, blurredCurr, cv::Size(blurK, blurK), 0);

    const bool hasBackground = (!backgroundGray.empty()
                                && backgroundGray.size() == gray.size()
                                && backgroundGray.type() == CV_8UC1);
    if (hasBackground) {
        cv::Mat blurredBg;
        cv::GaussianBlur(backgroundGray(cvRoi), blurredBg, cv::Size(blurK, blurK), 0);
        cv::subtract(blurredCurr, blurredBg, diffForProcessing);
    } else {
        diffForProcessing = blurredCurr;
    }

    cv::threshold(diffForProcessing, thresh, threshVal, 255, cv::THRESH_BINARY);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(morphK, morphK));
    cv::morphologyEx(thresh, roiDst, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), morphIter);
    cv::morphologyEx(roiDst, roiDst, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), morphIter);

    // Validation + contour/metric extraction (same helper as realtime)
    out.validation = filterProcessedImage(mask, cvRoi, config, gray);
    out.processedImage = std::move(mask);
    return out;
}

std::vector<ProcessedFrame> ProcessingService::processBatch(
    const std::vector<cv::Mat>& grayImages,
    const ProcessingConfig& config,
    const cv::Mat& background,
    const Roi& roi,
    BatchProgressCallback progress) {

    std::vector<ProcessedFrame> results;
    results.reserve(grayImages.size());

    const size_t total = grayImages.size();
    if (progress) progress(BatchProgress{0, total});

    for (size_t i = 0; i < total; ++i) {
        ProcessedFrame base = computeProcessedFrame(grayImages[i], background, config, roi,
                                                    static_cast<uint64_t>(i), 0);
        if (base.originalImage.empty() || base.processedImage.empty()) {
            results.emplace_back(std::move(base));
            if (progress) progress(BatchProgress{i + 1, total});
            continue;
        }

        Roi normalizedRoi = roi;
        if (normalizedRoi.w <= 0 || normalizedRoi.h <= 0) {
            normalizedRoi.x = 0;
            normalizedRoi.y = 0;
            normalizedRoi.w = base.originalImage.cols;
            normalizedRoi.h = base.originalImage.rows;
        }
        normalizedRoi.x = std::max(0, std::min(normalizedRoi.x, base.originalImage.cols - 1));
        normalizedRoi.y = std::max(0, std::min(normalizedRoi.y, base.originalImage.rows - 1));
        normalizedRoi.w = std::max(1, std::min(normalizedRoi.w, base.originalImage.cols - normalizedRoi.x));
        normalizedRoi.h = std::max(1, std::min(normalizedRoi.h, base.originalImage.rows - normalizedRoi.y));
        const cv::Rect cvRoi(normalizedRoi.x, normalizedRoi.y, normalizedRoi.w, normalizedRoi.h);

        auto objectResults = filterProcessedObjects(base.processedImage, cvRoi, config, base.originalImage);
        if (objectResults.empty()) {
            results.emplace_back(std::move(base));
        } else {
            for (auto& validation : objectResults) {
                ProcessedFrame objectFrame;
                objectFrame.index = base.index;
                objectFrame.timestampNs = base.timestampNs;
                objectFrame.originalImage = base.originalImage.clone();
                objectFrame.processedImage = base.processedImage.clone();
                objectFrame.validation = std::move(validation);
                results.emplace_back(std::move(objectFrame));
            }
        }
        if (progress) progress(BatchProgress{i + 1, total});
    }

    SPDLOG_INFO("processBatch: processed {} images into {} records (roi={}x{} at {},{}, background={})",
                total, results.size(), roi.w, roi.h, roi.x, roi.y, !background.empty());
    return results;
}

void ProcessingService::setRingRatioCallback(RingRatioCallback callback) {
    std::scoped_lock lk(ringRatioCallbackMutex_);
    ringRatioCallback_ = std::move(callback);
}

void ProcessingService::setTargetGroupCallback(TargetGroupCallback callback) {
    std::scoped_lock lk(targetGroupCallbackMutex_);
    targetGroupCallback_ = std::move(callback);
}

void ProcessingService::setBackgroundCaptureCallback(BackgroundCaptureCallback callback) {
    std::scoped_lock lk(backgroundCaptureCallbackMutex_);
    backgroundCaptureCallback_ = std::move(callback);
}

ProcessingService::DroppedFrameCounts ProcessingService::trimExperimentBuffersLocked(size_t maxBufferedFrames) {
    DroppedFrameCounts dropped{};
    if (maxBufferedFrames == 0) {
        maxBufferedFrames = 1;
    }

    while (validFrames_.size() + invalidFrames_.size() > maxBufferedFrames && !invalidFrames_.empty()) {
        invalidFrames_.erase(invalidFrames_.begin());
        ++dropped.invalid;
    }

    while (validFrames_.size() + invalidFrames_.size() > maxBufferedFrames && !validFrames_.empty()) {
        validFrames_.erase(validFrames_.begin());
        ++dropped.valid;
    }

    if (dropped.valid > 0) {
        droppedValidFrames_.fetch_add(static_cast<uint64_t>(dropped.valid), std::memory_order_relaxed);
    }
    if (dropped.invalid > 0) {
        droppedInvalidFrames_.fetch_add(static_cast<uint64_t>(dropped.invalid), std::memory_order_relaxed);
    }

    framesSinceLastFlush_.store(validFrames_.size() + invalidFrames_.size(), std::memory_order_relaxed);
    return dropped;
}

void ProcessingService::logDroppedExperimentFrames(const DroppedFrameCounts& dropped,
                                                   size_t bufferedTotal,
                                                   size_t maxBufferedFrames) {
    if (dropped.valid == 0 && dropped.invalid == 0) {
        return;
    }

    const uint64_t nowUs = backend::Tools::getTimestamp();
    uint64_t lastUs = lastDropLogUs_.load(std::memory_order_relaxed);
    if (nowUs - lastUs < 1'000'000ULL ||
        !lastDropLogUs_.compare_exchange_strong(lastUs, nowUs, std::memory_order_relaxed)) {
        return;
    }

    SPDLOG_WARN("Experiment frame backlog capped: dropped valid={}, invalid={} "
                "(buffered={}, max={}, total_dropped_valid={}, total_dropped_invalid={})",
                dropped.valid,
                dropped.invalid,
                bufferedTotal,
                maxBufferedFrames,
                droppedValidFrames_.load(std::memory_order_relaxed),
                droppedInvalidFrames_.load(std::memory_order_relaxed));
}

bool ProcessingService::appendExperimentFrame(ProcessedFrame&& frame, bool isValid) {
    DroppedFrameCounts dropped{};
    size_t bufferedTotal = 0;
    bool stored = false;
    const size_t maxBufferedFrames = std::max<size_t>(1, maxBufferedFrames_.load(std::memory_order_relaxed));

    {
        std::scoped_lock framesLk(framesMutex_);
        const size_t currentTotal = validFrames_.size() + invalidFrames_.size();

        if (currentTotal >= maxBufferedFrames) {
            if (isValid && !invalidFrames_.empty()) {
                invalidFrames_.erase(invalidFrames_.begin());
                ++dropped.invalid;
            } else {
                if (isValid) {
                    ++dropped.valid;
                    droppedValidFrames_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    ++dropped.invalid;
                    droppedInvalidFrames_.fetch_add(1, std::memory_order_relaxed);
                }
                bufferedTotal = currentTotal;
                framesSinceLastFlush_.store(bufferedTotal, std::memory_order_relaxed);
            }
        }

        if (bufferedTotal == 0) {
            if (isValid) {
                validFrames_.emplace_back(std::move(frame));
            } else {
                invalidFrames_.emplace_back(std::move(frame));
            }
            if (dropped.invalid > 0) {
                droppedInvalidFrames_.fetch_add(static_cast<uint64_t>(dropped.invalid), std::memory_order_relaxed);
            }

            DroppedFrameCounts extraDropped = trimExperimentBuffersLocked(maxBufferedFrames);
            dropped.valid += extraDropped.valid;
            dropped.invalid += extraDropped.invalid;
            bufferedTotal = validFrames_.size() + invalidFrames_.size();
            framesSinceLastFlush_.store(bufferedTotal, std::memory_order_relaxed);
            stored = true;
        }
    }

    logDroppedExperimentFrames(dropped, bufferedTotal, maxBufferedFrames);
    return stored;
}

size_t ProcessingService::flushBufferedFrames(class Hdf5Service& hdf5) {
    std::vector<ProcessedFrame> validToFlush;
    std::vector<ProcessedFrame> invalidToFlush;
    const double memBeforeMB = backend::Tools::getProcessMemoryMB();
    
    {
        std::scoped_lock lk(framesMutex_);
        if (validFrames_.empty() && invalidFrames_.empty()) {
            return 0;
        }
        
        // Move frames to flush (clears the buffers)
        validToFlush = std::move(validFrames_);
        invalidToFlush = std::move(invalidFrames_);
        validFrames_.clear();
        invalidFrames_.clear();
    }
    
    // Append to HDF5 file
    if (!validToFlush.empty() || !invalidToFlush.empty()) {
        using clock = std::chrono::steady_clock;
        const size_t validCount = validToFlush.size();
        const size_t invalidCount = invalidToFlush.size();
        SPDLOG_INFO("HDF5 flush start: valid={}, invalid={}, mem_mb_before={:.1f}", validCount, invalidCount, memBeforeMB);
        const auto t0 = clock::now();
        const bool ok = hdf5.appendFrames(validToFlush, invalidToFlush);
        const auto t1 = clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double memAfterMB = backend::Tools::getProcessMemoryMB();

        if (ok) {
            size_t flushed = validCount + invalidCount;
            if (validCount > 0) {
                totalValidFlushed_.fetch_add(static_cast<uint64_t>(validCount), std::memory_order_relaxed);
            }
            {
                std::scoped_lock lk(framesMutex_);
                framesSinceLastFlush_.store(validFrames_.size() + invalidFrames_.size(), std::memory_order_relaxed);
            }
            SPDLOG_INFO("HDF5 flush end: flushed={} (valid={}, invalid={}) duration_ms={:.3f} mem_mb_after={:.1f}",
                        flushed, validCount, invalidCount, ms, memAfterMB);
            return flushed;
        } else {
            DroppedFrameCounts dropped{};
            size_t bufferedTotal = 0;
            const size_t maxBufferedFrames = std::max<size_t>(1, maxBufferedFrames_.load(std::memory_order_relaxed));
            {
                // Restore failed frames ahead of frames accumulated during the write,
                // then re-apply the backlog cap so a persistent HDF5 failure cannot
                // retry itself into unbounded RAM growth.
                std::scoped_lock lk(framesMutex_);
                validFrames_.insert(validFrames_.begin(),
                                    std::make_move_iterator(validToFlush.begin()),
                                    std::make_move_iterator(validToFlush.end()));
                invalidFrames_.insert(invalidFrames_.begin(),
                                      std::make_move_iterator(invalidToFlush.begin()),
                                      std::make_move_iterator(invalidToFlush.end()));
                dropped = trimExperimentBuffersLocked(maxBufferedFrames);
                bufferedTotal = validFrames_.size() + invalidFrames_.size();
            }
            logDroppedExperimentFrames(dropped, bufferedTotal, maxBufferedFrames);
            SPDLOG_ERROR("HDF5 flush failed after {:.3f} ms; frames restored (valid={}, invalid={}), mem_mb_after_fail={:.1f}",
                         ms, validCount, invalidCount, memAfterMB);
            return 0;
        }
    }
    
    return 0;
}

void ProcessingService::setFlushInterval(size_t frames) {
    if (frames == 0) frames = 1; // Minimum 1
    flushInterval_.store(frames);
    const size_t maxBuffered = defaultMaxBufferedFrames(frames);
    maxBufferedFrames_.store(maxBuffered, std::memory_order_relaxed);
    SPDLOG_INFO("Flush interval set to: {} frames (max buffered backlog: {})", frames, maxBuffered);
}

size_t ProcessingService::getFlushInterval() const {
    return flushInterval_.load();
}

size_t ProcessingService::getMaxBufferedFrames() const {
    return maxBufferedFrames_.load(std::memory_order_relaxed);
}

void ProcessingService::setInvalidFrameSamplingRate(size_t rate) {
    if (rate == 0) rate = 1; // Minimum 1 (save all)
    invalidFrameSamplingRate_.store(rate);
    SPDLOG_INFO("Invalid frame sampling rate set to: every {}th frame", rate);
}

size_t ProcessingService::getInvalidFrameSamplingRate() const {
    return invalidFrameSamplingRate_.load();
}

double ProcessingService::calculateRingRatio(const std::vector<cv::Point>& innerContour, const std::vector<cv::Point>& outerContour) {
    double innerArea = cv::contourArea(innerContour);
    double outerArea = cv::contourArea(outerContour);
    if (outerArea <= innerArea) return 0.0;
    return std::sqrt(outerArea - innerArea);
}

ProcessingService::ContourAnalysis ProcessingService::findContours(const cv::Mat& processedImage) {
    ContourAnalysis analysis;
    cv::findContours(processedImage, analysis.allContours, analysis.hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

    const double minNoiseArea = 10.0;

    for (size_t i = 0; i < analysis.allContours.size(); i++) {
        double area = cv::contourArea(analysis.allContours[i]);
        if (area >= minNoiseArea) {
            analysis.filteredContours.push_back(analysis.allContours[i]);
            analysis.originalIndices.push_back(i);
        }
    }

    for (size_t i = 0; i < analysis.originalIndices.size(); i++) {
        size_t origIdx = analysis.originalIndices[i];
        if (origIdx < analysis.hierarchy.size() && analysis.hierarchy[origIdx][3] > -1) {
            analysis.innerContours.push_back(analysis.filteredContours[i]);
            analysis.innerFilteredIndices.push_back(static_cast<int>(i));
            int parentOrigIdx = analysis.hierarchy[origIdx][3];
            int filteredParentIdx = -1;
            for (size_t j = 0; j < analysis.originalIndices.size(); j++) {
                if (analysis.originalIndices[j] == static_cast<size_t>(parentOrigIdx)) {
                    filteredParentIdx = static_cast<int>(j);
                    break;
                }
            }
            analysis.parentIndices.push_back(filteredParentIdx);
        }
    }

    return analysis;
}

BrightnessQuantiles ProcessingService::calculateBrightnessQuantiles(const cv::Mat& originalImage, const cv::Mat& mask) {
    BrightnessQuantiles result;
    cv::Mat grayImage;
    if (originalImage.channels() == 3) {
        cv::cvtColor(originalImage, grayImage, cv::COLOR_BGR2GRAY);
    } else {
        grayImage = originalImage.clone();
    }

    std::vector<uchar> brightness;
    brightness.reserve(grayImage.rows * grayImage.cols / 4);

    for (int y = 0; y < grayImage.rows; y++) {
        for (int x = 0; x < grayImage.cols; x++) {
            if (mask.at<uchar>(y, x) > 0) {
                brightness.push_back(grayImage.at<uchar>(y, x));
            }
        }
    }

    if (brightness.empty()) return result;

    std::sort(brightness.begin(), brightness.end());
    size_t n = brightness.size();
    result.q1 = brightness[n / 4];
    result.q2 = brightness[n / 2];
    result.q3 = brightness[(3 * n) / 4];
    result.q4 = brightness[n - 1];

    return result;
}

cv::Mat ProcessingService::makeObjectMask(const cv::Size& size,
                                          const std::vector<std::vector<cv::Point>>& contours,
                                          int contourIdx,
                                          int parentIdx,
                                          bool nested) const {
    cv::Mat mask(size, CV_8UC1, cv::Scalar(0));
    if (nested && parentIdx >= 0 && parentIdx < static_cast<int>(contours.size())) {
        cv::drawContours(mask, contours, parentIdx, cv::Scalar(255), cv::FILLED);
        if (contourIdx >= 0 && contourIdx < static_cast<int>(contours.size())) {
            cv::drawContours(mask, contours, contourIdx, cv::Scalar(0), cv::FILLED);
        }
    } else if (contourIdx >= 0 && contourIdx < static_cast<int>(contours.size())) {
        cv::drawContours(mask, contours, contourIdx, cv::Scalar(255), cv::FILLED);
    }
    return mask;
}

bool ProcessingService::contourTouchesRoiBorder(const std::vector<cv::Point>& contour, const cv::Rect& roi) const {
    constexpr int borderThreshold = 2;
    for (const auto& point : contour) {
        const int x = point.x - roi.x;
        const int y = point.y - roi.y;
        if (x >= 0 && x < roi.width && y >= 0 && y < roi.height) {
            if (x < borderThreshold || x >= roi.width - borderThreshold ||
                y < borderThreshold || y >= roi.height - borderThreshold) {
                return true;
            }
        } else {
            return true;
        }
    }
    return false;
}

FilterResult ProcessingService::evaluateInnerContourObject(const ContourAnalysis& analysis,
                                                           size_t innerIdx,
                                                           int objectId,
                                                           int objectCount,
                                                           const cv::Mat& processedImage,
                                                           const cv::Rect& roi,
                                                           const ProcessingConfig& config,
                                                           const cv::Mat& originalImage) {
    FilterResult result{};
    result.allContours = analysis.allContours;
    result.hierarchy = analysis.hierarchy;
    result.innerContourCount = static_cast<int>(analysis.innerContours.size());
    result.hasSingleInnerContour = (analysis.innerContours.size() == 1);
    result.objectId = objectId;
    result.objectCount = objectCount;

    if (innerIdx >= analysis.innerContours.size()) {
        return result;
    }

    const auto& innerContour = analysis.innerContours[innerIdx];
    const int parentIdx = innerIdx < analysis.parentIndices.size() ? analysis.parentIndices[innerIdx] : -1;
    const int innerFilteredIdx = innerIdx < analysis.innerFilteredIndices.size() ? analysis.innerFilteredIndices[innerIdx] : -1;

    const cv::Mat objectMask = makeObjectMask(processedImage.size(), analysis.filteredContours,
                                              innerFilteredIdx, parentIdx, true);
    if (!originalImage.empty()) {
        result.brightness = calculateBrightnessQuantiles(originalImage, objectMask);
    }

    if (config.enable_border_check && contourTouchesRoiBorder(innerContour, roi)) {
        result.touchesBorder = true;
        return result;
    }

    const double contourArea = cv::contourArea(innerContour);
    if (contourArea <= 0.0) {
        return result;
    }

    std::vector<cv::Point> hull;
    cv::convexHull(innerContour, hull);
    const double hullArea = cv::contourArea(hull);
    result.areaRatio = hullArea / contourArea;
    const double perimeter = cv::arcLength(hull, true);
    const double circularity = (perimeter > 0.0) ? std::sqrt(4 * M_PI * hullArea) / perimeter : 0.0;
    result.deformability = 1.0 - circularity;
    result.area = hullArea;

    if (parentIdx >= 0 && parentIdx < static_cast<int>(analysis.filteredContours.size())) {
        result.ringRatio = calculateRingRatio(innerContour, analysis.filteredContours[parentIdx]);
    }

    const double pxToUm = pixelToMicronFactor_.load(std::memory_order_relaxed);
    const double areaUm = hullArea * pxToUm * pxToUm;

    const bool areaInRange = !config.enable_area_range_check ||
        (areaUm >= config.area_threshold_min && areaUm <= config.area_threshold_max);
    const bool ringRatioInRange = !config.enable_ring_ratio_check ||
        (result.ringRatio > config.ring_ratio_min && result.ringRatio < config.ring_ratio_max);
    const bool deformabilityInRange = !config.enable_deformability_range_check ||
        (result.deformability >= config.deformability_threshold_min &&
         result.deformability <= config.deformability_threshold_max);
    const bool areaRatioInRange = !config.enable_area_ratio_check ||
        (result.areaRatio <= config.area_ratio_threshold_max);

    if (areaInRange && ringRatioInRange && deformabilityInRange && areaRatioInRange) {
        result.inRange = true;
        result.isValid = true;
    }

    if (eModulusLut_.isLoaded()) {
        result.youngsModulus = eModulusLut_.lookup(areaUm, result.deformability);
    }
    if (result.isValid && config.enable_target_group) {
        const bool tgArea = (areaUm >= config.target_group_area_min &&
                             areaUm <= config.target_group_area_max);
        const bool tgDeform = (result.deformability >= config.target_group_deformability_min &&
                               result.deformability <= config.target_group_deformability_max);
        const bool tgEmod = !config.enable_target_group_emodulus ||
            (!std::isnan(result.youngsModulus) &&
             result.youngsModulus >= config.target_group_emodulus_min &&
             result.youngsModulus <= config.target_group_emodulus_max);
        result.isTargetGroup = tgArea && tgDeform && tgEmod;
    }

    return result;
}

FilterResult ProcessingService::evaluateOuterContourObject(const ContourAnalysis& analysis,
                                                           size_t contourIdx,
                                                           int objectId,
                                                           int objectCount,
                                                           const cv::Mat& processedImage,
                                                           const cv::Rect& roi,
                                                           const ProcessingConfig& config,
                                                           const cv::Mat& originalImage) {
    FilterResult result{};
    result.allContours = analysis.allContours;
    result.hierarchy = analysis.hierarchy;
    result.innerContourCount = static_cast<int>(analysis.innerContours.size());
    result.hasSingleInnerContour = (analysis.innerContours.size() == 1);
    result.objectId = objectId;
    result.objectCount = objectCount;

    if (contourIdx >= analysis.filteredContours.size()) {
        return result;
    }

    const auto& contour = analysis.filteredContours[contourIdx];
    const cv::Mat objectMask = makeObjectMask(processedImage.size(), analysis.filteredContours,
                                              static_cast<int>(contourIdx), -1, false);
    if (!originalImage.empty()) {
        result.brightness = calculateBrightnessQuantiles(originalImage, objectMask);
    }

    if (config.enable_border_check && contourTouchesRoiBorder(contour, roi)) {
        result.touchesBorder = true;
        return result;
    }

    const double contourArea = cv::contourArea(contour);
    if (contourArea <= 0.0) {
        return result;
    }

    std::vector<cv::Point> hull;
    cv::convexHull(contour, hull);
    const double hullArea = cv::contourArea(hull);
    result.areaRatio = hullArea / contourArea;
    const double perimeter = cv::arcLength(hull, true);
    const double circularity = (perimeter > 0.0) ? std::sqrt(4 * M_PI * hullArea) / perimeter : 0.0;
    result.deformability = 1.0 - circularity;
    result.area = hullArea;

    const double pxToUm = pixelToMicronFactor_.load(std::memory_order_relaxed);
    const double areaUm = hullArea * pxToUm * pxToUm;

    const bool areaInRange = !config.enable_area_range_check ||
        (areaUm >= config.area_threshold_min && areaUm <= config.area_threshold_max);
    const bool deformabilityInRange = !config.enable_deformability_range_check ||
        (result.deformability >= config.deformability_threshold_min &&
         result.deformability <= config.deformability_threshold_max);
    const bool areaRatioInRange = !config.enable_area_ratio_check ||
        (result.areaRatio <= config.area_ratio_threshold_max);

    if (areaInRange && deformabilityInRange && areaRatioInRange) {
        result.inRange = true;
        result.isValid = true;
    }

    if (eModulusLut_.isLoaded()) {
        result.youngsModulus = eModulusLut_.lookup(areaUm, result.deformability);
    }
    if (result.isValid && config.enable_target_group) {
        const bool tgArea = (areaUm >= config.target_group_area_min &&
                             areaUm <= config.target_group_area_max);
        const bool tgDeform = (result.deformability >= config.target_group_deformability_min &&
                               result.deformability <= config.target_group_deformability_max);
        const bool tgEmod = !config.enable_target_group_emodulus ||
            (!std::isnan(result.youngsModulus) &&
             result.youngsModulus >= config.target_group_emodulus_min &&
             result.youngsModulus <= config.target_group_emodulus_max);
        result.isTargetGroup = tgArea && tgDeform && tgEmod;
    }

    return result;
}

std::vector<FilterResult> ProcessingService::filterProcessedObjects(const cv::Mat& processedImage, const cv::Rect& roi,
                                                                    const ProcessingConfig& config,
                                                                    const cv::Mat& originalImage) {
    const ContourAnalysis analysis = findContours(processedImage);

    FilterResult emptyResult{};
    emptyResult.allContours = analysis.allContours;
    emptyResult.hierarchy = analysis.hierarchy;
    emptyResult.innerContourCount = static_cast<int>(analysis.innerContours.size());
    emptyResult.hasSingleInnerContour = (analysis.innerContours.size() == 1);
    if (!originalImage.empty()) {
        emptyResult.brightness = calculateBrightnessQuantiles(originalImage, processedImage);
    }

    if (config.require_single_inner_contour && analysis.innerContours.empty()) {
        return {std::move(emptyResult)};
    }

    if (!analysis.innerContours.empty()) {
        std::vector<FilterResult> results;
        results.reserve(analysis.innerContours.size());
        const int objectCount = static_cast<int>(analysis.innerContours.size());
        for (size_t i = 0; i < analysis.innerContours.size(); ++i) {
            results.push_back(evaluateInnerContourObject(analysis, i, static_cast<int>(i), objectCount,
                                                         processedImage, roi, config, originalImage));
        }
        return results;
    }

    if (!analysis.filteredContours.empty() && !config.require_single_inner_contour) {
        std::vector<size_t> topLevelContours;
        for (size_t i = 0; i < analysis.filteredContours.size(); ++i) {
            const size_t origIdx = i < analysis.originalIndices.size() ? analysis.originalIndices[i] : 0;
            const bool hasParent = origIdx < analysis.hierarchy.size() && analysis.hierarchy[origIdx][3] > -1;
            if (!hasParent) {
                topLevelContours.push_back(i);
            }
        }
        if (topLevelContours.empty()) {
            for (size_t i = 0; i < analysis.filteredContours.size(); ++i) {
                topLevelContours.push_back(i);
            }
        }

        std::vector<FilterResult> results;
        results.reserve(topLevelContours.size());
        const int objectCount = static_cast<int>(topLevelContours.size());
        for (size_t i = 0; i < topLevelContours.size(); ++i) {
            results.push_back(evaluateOuterContourObject(analysis, topLevelContours[i], static_cast<int>(i),
                                                         objectCount, processedImage, roi, config, originalImage));
        }
        return results;
    }

    return {std::move(emptyResult)};
}

FilterResult ProcessingService::filterProcessedImage(const cv::Mat& processedImage, const cv::Rect& roi,
                                                     const ProcessingConfig& config, const cv::Mat& originalImage) {
    auto results = filterProcessedObjects(processedImage, roi, config, originalImage);
    if (results.empty()) {
        return {};
    }
    return std::move(results.front());
}

void ProcessingService::realtimeLoop() {
    rtLastProcessed_.store(0);
    using clock = std::chrono::steady_clock;
    auto lastSummaryTs = clock::now();
    uint64_t framesSinceSummary = 0;
    uint64_t framesSkippedSinceSummary = 0;
    double msSinceSummary = 0.0;
    double algoMsSinceSummary = 0.0;
    uint64_t validSinceSummary = 0;
    uint64_t invalidSinceSummary = 0;

    // Multi-image series state (persists across loop iterations, single-threaded access)
    ProcessedFrame pendingMultiImageFrame;
    size_t multiImageRemaining = 0; // frames still needed to complete current series
    bool multiImagePending = false;

    while (rtRunning_.load()) {
        if (!rtStore_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const uint64_t total = rtStore_->totalWritten();
        if (total == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        const uint64_t earliest = rtStore_->earliestAvailableIndex();
        const uint64_t latest = rtStore_->latestAvailableIndex();
        uint64_t last = rtLastProcessed_.load();
        if (last + 1 < earliest) {
            // Skip ahead if our pointer fell behind the ring window
            uint64_t skipped = earliest - (last + 1);
            framesSkippedSinceSummary += skipped;
            last = earliest - 1;
            SPDLOG_DEBUG("Processing fell behind, skipping {} frames (last={}, earliest={})", skipped, last, earliest);
        }
        if (last >= latest) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        // If enabled, prefer processing only the most recent frame to minimize latency (drop intermediate frames).
        // We intentionally ignore this mode during experiments to avoid dropping frames that might be saved.
        const bool dropFrames = rtDropFrames_.load(std::memory_order_relaxed) && !experimentActive_.load();
        if (dropFrames) {
            const uint64_t nextIdx = latest;
            if (last + 1 < nextIdx) {
                framesSkippedSinceSummary += (nextIdx - (last + 1));
            }
            const uint64_t idx = nextIdx;
            const auto frameStart = clock::now();
            if (!rtEnabled_.load()) { rtLastProcessed_.store(idx); continue; }
            
            // Get ROI and config first (outside frame access to minimize lock time)
            Roi roi{};
            std::shared_ptr<cv::Mat> bgShared;
            ProcessingConfig config;
            {
                std::scoped_lock lk(rtMutex_);
                roi = rtRoi_;
                bgShared = rtBgGray_; // shared_ptr copy is cheap, no cloning
            }
            {
                std::scoped_lock cfgLk(configMutex_);
                config = processingConfig_;
            }
            
            // Get frame - use ROI access if ROI is specified, otherwise full frame
            backend::playback::Frame f{};
            bool useROI = (roi.w > 0 && roi.h > 0);
            if (useROI) {
                // Clamp ROI to reasonable bounds first
                if (!rtStore_->getByWriteIndex(idx, f)) {
                    continue;
                }
                if (f.width == 0 || f.height == 0 || f.data.empty()) {
                    continue;
                }
                const int frameW = static_cast<int>(f.width);
                const int frameH = static_cast<int>(f.height);
                roi.x = std::max(0, std::min(roi.x, frameW - 1));
                roi.y = std::max(0, std::min(roi.y, frameH - 1));
                roi.w = std::max(1, std::min(roi.w, frameW - roi.x));
                roi.h = std::max(1, std::min(roi.h, frameH - roi.y));
                
                // Extract ROI directly from frame
                cv::Mat grayROI = makeGrayROI(f, roi.x, roi.y, roi.w, roi.h);
                if (grayROI.empty()) {
                    continue;
                }
                
                // Create ROI-sized mask (much smaller than full frame)
                cv::Mat mask(roi.h, roi.w, CV_8UC1, cv::Scalar(0));
                cv::Mat blurredCurr, blurredBg, thresh;
                const auto algoStart = clock::now();
                auto toOdd = [](int v) -> int { if (v < 1) v = 1; if ((v % 2) == 0) v += 1; return v; };
                const int blurK = toOdd(config.gaussian_blur_size);
                const int morphK = toOdd(config.morph_kernel_size);
                const int morphIter = std::max(1, config.morph_iterations);
                const int threshVal = std::max(0, config.bg_subtract_threshold);

                cv::GaussianBlur(grayROI, blurredCurr, cv::Size(blurK, blurK), 0);
                bool hasBackground = (bgShared && !bgShared->empty() && bgShared->size() == cv::Size(static_cast<int>(f.width), static_cast<int>(f.height)) && bgShared->type() == CV_8UC1);
                
                // For processing: use background subtraction if available
                cv::Mat diffForProcessing;
                if (hasBackground) {
                    cv::Rect bgRoi(roi.x, roi.y, roi.w, roi.h);
                    cv::Mat bgROI = (*bgShared)(bgRoi);
                    cv::GaussianBlur(bgROI, blurredBg, cv::Size(blurK, blurK), 0);
                    cv::subtract(blurredCurr, blurredBg, diffForProcessing);
                } else {
                    diffForProcessing = blurredCurr;
                }
                
                // For auto-capture detection: always use frame-to-frame difference when enabled
                cv::Mat diffForAutoCapture;
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    if (!previousFrameForAutoCapture_.empty() && 
                        previousFrameForAutoCapture_.size() == blurredCurr.size() &&
                        previousFrameForAutoCapture_.type() == blurredCurr.type()) {
                        cv::absdiff(blurredCurr, previousFrameForAutoCapture_, diffForAutoCapture);
                    } else {
                        // First frame or size mismatch: store current frame and skip auto-capture check
                        previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                        diffForAutoCapture = blurredCurr; // Use current frame for thresholding (will not be empty)
                    }
                } else {
                    diffForAutoCapture = diffForProcessing; // Fallback to processing diff
                }
                
                // Use frame-to-frame diff for empty frame detection when auto-capture is enabled
                cv::Mat diff = (config.auto_background_enabled && !experimentActive_.load()) ? diffForAutoCapture : diffForProcessing;
                cv::threshold(diff, thresh, threshVal, 255, cv::THRESH_BINARY);
                
                // Check for empty frame: count non-zero pixels after binary threshold
                int pixelCount = cv::countNonZero(thresh);
                if (pixelCount < config.empty_frame_pixel_threshold) {
                    SPDLOG_TRACE("Empty frame detected (idx={}, pixel_count={}, threshold={}), skipping further processing",
                                idx, pixelCount, config.empty_frame_pixel_threshold);
                    
                    // Auto-capture logic (only when experiment is NOT running)
                    if (config.auto_background_enabled && !experimentActive_.load()) {
                        uint64_t currentEmpty = consecutiveEmptyFrames_.fetch_add(1, std::memory_order_relaxed) + 1;
                        uint64_t lastCapture = lastAutoBackgroundFrame_.load(std::memory_order_relaxed);
                        uint64_t framesSinceCapture = (idx > lastCapture) ? (idx - lastCapture) : 0;
                        
                        // Check if we should capture: enough consecutive empty frames AND cooldown period passed
                        if (currentEmpty >= static_cast<uint64_t>(config.auto_background_empty_frames) &&
                            framesSinceCapture >= static_cast<uint64_t>(config.auto_background_cooldown_frames)) {
                            
                            // Capture full frame as background (not just ROI)
                            cv::Mat fullGray = makeGrayCopy(f.width, f.height, f.linePitch, f.data.data());
                            if (!fullGray.empty()) {
                                setRealtimeBackgroundGray(fullGray);
                                lastAutoBackgroundFrame_.store(idx, std::memory_order_relaxed);
                                consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                                
                                // Update previous frame cache to current frame (for next frame-to-frame comparison)
                                {
                                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                                }
                                
                                // Notify via callback
                                {
                                    std::scoped_lock callbackLk(backgroundCaptureCallbackMutex_);
                                    if (backgroundCaptureCallback_) {
                                        backgroundCaptureCallback_(fullGray.clone(), idx);
                                    }
                                }
                                
                                SPDLOG_INFO("Auto-captured background at frame {} ({} consecutive empty frames)", 
                                           idx, currentEmpty);
                            }
                        }
                    } else {
                        // Reset counter if auto-capture disabled, experiment running, or movement detected
                        if (!config.auto_background_enabled || experimentActive_.load()) {
                            consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                        }
                    }
                    
                    rtLastProcessed_.store(idx);
                    continue; // Skip morphology, contours, validation, and frame accumulation
                }
                
                // Reset counter on non-empty frames
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                }
                
                // Update previous frame for frame-to-frame comparison (always when auto-capture enabled)
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                }
                
                // Use background subtraction diff for actual processing (morphology, contours, etc.)
                cv::threshold(diffForProcessing, thresh, threshVal, 255, cv::THRESH_BINARY);
                cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(morphK, morphK));
                cv::morphologyEx(thresh, mask, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), morphIter);
                cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), morphIter);

                // Always run validation for monitoring (even without experiment)
                // mask is ROI-sized so contour coords are 0-based; use local roi for border check
                cv::Rect localRoi(0, 0, roi.w, roi.h);
                FilterResult validation = filterProcessedImage(mask, localRoi, config, grayROI);

                // Extract contours from validation result and adjust coordinates for full-frame snapshot
                // Contours from filterProcessedImage are in ROI coordinates, need to adjust for full frame
                std::vector<std::vector<cv::Point>> contours = validation.allContours;
                for (auto& contour : contours) {
                    for (auto& pt : contour) {
                        pt.x += roi.x;
                        pt.y += roi.y;
                    }
                }
                const auto algoEnd = clock::now();
                const double algoMs = std::chrono::duration<double, std::milli>(algoEnd - algoStart).count();
                algoMsSinceSummary += algoMs;
                if (validation.isValid) {
                    ++validSinceSummary;
                } else {
                    ++invalidSinceSummary;
                }
                
                // Fire trigger + autofocus callbacks BEFORE taking monitoringFramesMutex_
                // so the UI thread's ring-buffer snapshot cannot stall the trigger path.
                // Target-group fires FIRST; RingRatio callback is O(1) on the realtime
                // thread as of 2026-04-16 (push into AutofocusService::pendingSamples_
                // + notify_one; the O(n log n) sort runs on AutofocusService::statsThread_).
                if (validation.isValid) {
                    TargetGroupCallback tgCb;
                    {
                        std::scoped_lock cbLk(targetGroupCallbackMutex_);
                        tgCb = targetGroupCallback_;
                    }
                    if (tgCb) tgCb(validation.isTargetGroup);

                    if (validation.ringRatio > 0.0) {
                        RingRatioCallback rrCb;
                        {
                            std::scoped_lock cbLk(ringRatioCallbackMutex_);
                            rrCb = ringRatioCallback_;
                        }
                        if (rrCb) rrCb(validation.ringRatio, f.timestamp);
                    }
                }

                // Always accumulate frames for monitoring (with size limit)
                {
                    ProcessedFrame monitoringFrame;
                    monitoringFrame.index = idx;
                    monitoringFrame.timestampNs = f.timestamp;
                    monitoringFrame.validation = validation;
                    // Store ROI-only images (already ROI-sized, just clone)
                    monitoringFrame.originalImage = grayROI.clone();
                    monitoringFrame.processedImage = mask.clone();

                std::scoped_lock monitoringLk(monitoringFramesMutex_);
                if (validation.isValid) {
                    monitoringValidFrames_.push_back(std::move(monitoringFrame));
                } else {
                    monitoringInvalidFrames_.push_back(std::move(monitoringFrame));
                }

                // Throttled DEBUG: accumulation sizes and process memory
                if ((idx % 5000ULL) == 0ULL) {
                    size_t vSz = 0;
                    size_t iSz = 0;
                    {
                        std::scoped_lock fLk(framesMutex_);
                        vSz = validFrames_.size();
                        iSz = invalidFrames_.size();
                    }
                    SPDLOG_TRACE("Accumulated frames (idx={}): valid={}, invalid={}, flush_interval={}, since_last_flush={}, mem_mb={:.1f}",
                                 idx, vSz, iSz, flushInterval_.load(), framesSinceLastFlush_.load(), backend::Tools::getProcessMemoryMB());
                }
            }

            // Throttled DEBUG: monitoring buffer sizes and process memory
            if ((idx % 5000ULL) == 0ULL) {
                size_t monValidSz = 0;
                size_t monInvalidSz = 0;
                {
                    std::scoped_lock mLk(monitoringFramesMutex_);
                    monValidSz = monitoringValidFrames_.size();
                    monInvalidSz = monitoringInvalidFrames_.size();
                }
                SPDLOG_TRACE("Realtime monitoring sizes (idx={}): mon_valid={}, mon_invalid={}, mem_mb={:.1f}",
                             idx, monValidSz, monInvalidSz, backend::Tools::getProcessMemoryMB());
            }
            
            // Create full frame copy outside algo timing, only when needed for experiment/snapshot
            cv::Mat grayFull;

            // Also accumulate frames for experiment if active
            if (experimentActive_.load()) {
                const bool multiImageMode = config.multi_image_enabled && config.multi_image_count > 1;

                // Helper: create full frame from ROI path data
                auto makeFullGray = [&]() -> cv::Mat {
                    if (grayFull.empty()) {
                        grayFull = makeGrayCopy(f.width, f.height, f.linePitch, f.data.data());
                    }
                    return grayFull.clone();
                };

                if (multiImagePending) {
                    // Collecting series images for pending multi-image trigger
                    pendingMultiImageFrame.seriesImages.push_back(makeFullGray());
                    --multiImageRemaining;
                    SPDLOG_TRACE("Multi-image series (ROI path): captured frame {} (remaining={})", idx, multiImageRemaining);

                    if (multiImageRemaining == 0) {
                        multiImagePending = false;
                        SPDLOG_DEBUG("Multi-image series complete (ROI path): trigger_idx={}, series_size={}",
                                    pendingMultiImageFrame.index, pendingMultiImageFrame.seriesImages.size());
                        appendExperimentFrame(std::move(pendingMultiImageFrame), true);
                        pendingMultiImageFrame = ProcessedFrame{};
                    }
                } else {
                    bool shouldSave = false;
                    if (validation.isValid) {
                        if (multiImageMode) {
                            // Start new multi-image series
                            cv::Mat fullGray = makeFullGray();
                            cv::Mat fullMask(fullGray.rows, fullGray.cols, CV_8UC1, cv::Scalar(0));
                            cv::Rect fullCvRoi(roi.x, roi.y, roi.w, roi.h);
                            mask.copyTo(fullMask(fullCvRoi));

                            pendingMultiImageFrame = ProcessedFrame{};
                            pendingMultiImageFrame.index = idx;
                            pendingMultiImageFrame.timestampNs = f.timestamp;
                            pendingMultiImageFrame.validation = validation;
                            pendingMultiImageFrame.originalImage = fullGray.clone();
                            pendingMultiImageFrame.processedImage = fullMask.clone();
                            pendingMultiImageFrame.seriesImages.push_back(std::move(fullGray));
                            multiImageRemaining = static_cast<size_t>(config.multi_image_count - 1);
                            multiImagePending = true;
                            SPDLOG_DEBUG("Multi-image series started (ROI path): trigger_idx={}, count={}", idx, config.multi_image_count);
                        } else {
                            shouldSave = true;
                        }
                    } else {
                        size_t counter = invalidFrameCounter_.fetch_add(1, std::memory_order_relaxed);
                        size_t rate = invalidFrameSamplingRate_.load(std::memory_order_relaxed);
                        if (rate > 0 && (counter % rate) == 0) {
                            shouldSave = true;
                        }
                    }

                    if (shouldSave) {
                        ProcessedFrame frame;
                        frame.index = idx;
                        frame.timestampNs = f.timestamp;
                        frame.validation = validation;
                        cv::Mat fullGray = makeFullGray();
                        cv::Mat fullMask(fullGray.rows, fullGray.cols, CV_8UC1, cv::Scalar(0));
                        cv::Rect fullCvRoi(roi.x, roi.y, roi.w, roi.h);
                        mask.copyTo(fullMask(fullCvRoi));
                        frame.originalImage = fullGray.clone();
                        frame.processedImage = fullMask.clone();

                        appendExperimentFrame(std::move(frame), validation.isValid);
                    }
                }
            } else if (multiImagePending) {
                // Experiment ended while collecting series — save partial
                SPDLOG_WARN("Multi-image series incomplete (ROI path, experiment ended): trigger_idx={}, collected={}",
                            pendingMultiImageFrame.index, pendingMultiImageFrame.seriesImages.size());
                multiImagePending = false;
                multiImageRemaining = 0;
                appendExperimentFrame(std::move(pendingMultiImageFrame), true);
                pendingMultiImageFrame = ProcessedFrame{};
            }

                // Publish snapshot
                {
                    std::scoped_lock lk(snapshotMutex_);
                    latestSnapshot_.index = idx;
                    // Create full-size mask for snapshot display
                    cv::Mat fullMaskSnapshot;
                    if (useROI) {
                        // Reuse existing full frame if already created for experiment storage
                        cv::Mat grayFullSnap;
                        if (!grayFull.empty()) {
                            grayFullSnap = grayFull;
                        } else {
                            backend::playback::Frame fFull{};
                            if (rtStore_->getByWriteIndex(idx, fFull)) {
                                grayFullSnap = makeGrayCopy(fFull.width, fFull.height, fFull.linePitch, fFull.data.data());
                            }
                        }
                        if (!grayFullSnap.empty()) {
                            fullMaskSnapshot = cv::Mat(grayFullSnap.rows, grayFullSnap.cols, CV_8UC1, cv::Scalar(0));
                            cv::Rect fullCvRoiSnap(roi.x, roi.y, roi.w, roi.h);
                            mask.copyTo(fullMaskSnapshot(fullCvRoiSnap));
                        } else {
                            fullMaskSnapshot = mask.clone();
                        }
                    } else {
                        fullMaskSnapshot = mask.clone();
                    }
                    latestSnapshot_.mask = fullMaskSnapshot;
                    latestSnapshot_.contours = std::move(contours);
                    latestSnapshot_.validation = validation;
                }

                rtLastProcessed_.store(idx);
            } else {
                // No ROI specified - process full frame (fallback to original behavior)
                backend::playback::Frame f{};
                if (!rtStore_->getByWriteIndex(idx, f)) {
                    continue;
                }
                if (f.width == 0 || f.height == 0 || f.data.empty()) {
                    continue;
                }
                cv::Mat gray = makeGrayCopy(f.width, f.height, f.linePitch, f.data.data());

                // Clamp ROI (will be full frame if not set)
                if (roi.w <= 0 || roi.h <= 0) {
                    roi.x = 0; roi.y = 0; roi.w = gray.cols; roi.h = gray.rows;
                }
                roi.x = std::max(0, std::min(roi.x, gray.cols - 1));
                roi.y = std::max(0, std::min(roi.y, gray.rows - 1));
                roi.w = std::max(1, std::min(roi.w, gray.cols - roi.x));
                roi.h = std::max(1, std::min(roi.h, gray.rows - roi.y));

                cv::Rect cvRoi(roi.x, roi.y, roi.w, roi.h);

                // Build full-size mask
                cv::Mat mask(gray.rows, gray.cols, CV_8UC1, cv::Scalar(0));
                cv::Mat roiCurr = gray(cvRoi);
                cv::Mat roiDst = mask(cvRoi);
                cv::Mat blurredCurr, blurredBg, thresh;
                const auto algoStart = clock::now();
                auto toOdd = [](int v) -> int { if (v < 1) v = 1; if ((v % 2) == 0) v += 1; return v; };
                const int blurK = toOdd(config.gaussian_blur_size);
                const int morphK = toOdd(config.morph_kernel_size);
                const int morphIter = std::max(1, config.morph_iterations);
                const int threshVal = std::max(0, config.bg_subtract_threshold);

                cv::GaussianBlur(roiCurr, blurredCurr, cv::Size(blurK, blurK), 0);
                bool hasBackground = (bgShared && !bgShared->empty() && bgShared->size() == gray.size() && bgShared->type() == CV_8UC1);
                
                // For processing: use background subtraction if available
                cv::Mat diffForProcessing;
                if (hasBackground) {
                    cv::GaussianBlur((*bgShared)(cvRoi), blurredBg, cv::Size(blurK, blurK), 0);
                    cv::subtract(blurredCurr, blurredBg, diffForProcessing);
                } else {
                    diffForProcessing = blurredCurr;
                }
                
                // For auto-capture detection: always use frame-to-frame difference when enabled
                cv::Mat diffForAutoCapture;
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    if (!previousFrameForAutoCapture_.empty() && 
                        previousFrameForAutoCapture_.size() == blurredCurr.size() &&
                        previousFrameForAutoCapture_.type() == blurredCurr.type()) {
                        cv::absdiff(blurredCurr, previousFrameForAutoCapture_, diffForAutoCapture);
                    } else {
                        // First frame or size mismatch: store current frame and skip auto-capture check
                        previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                        diffForAutoCapture = blurredCurr; // Use current frame for thresholding (will not be empty)
                    }
                } else {
                    diffForAutoCapture = diffForProcessing; // Fallback to processing diff
                }
                
                // Use frame-to-frame diff for empty frame detection when auto-capture is enabled
                cv::Mat diff = (config.auto_background_enabled && !experimentActive_.load()) ? diffForAutoCapture : diffForProcessing;
                cv::threshold(diff, thresh, threshVal, 255, cv::THRESH_BINARY);
                
                // Check for empty frame: count non-zero pixels after binary threshold
                int pixelCount = cv::countNonZero(thresh);
                if (pixelCount < config.empty_frame_pixel_threshold) {
                    SPDLOG_TRACE("Empty frame detected (idx={}, pixel_count={}, threshold={}), skipping further processing",
                                idx, pixelCount, config.empty_frame_pixel_threshold);
                    
                    // Auto-capture logic (only when experiment is NOT running)
                    if (config.auto_background_enabled && !experimentActive_.load()) {
                        uint64_t currentEmpty = consecutiveEmptyFrames_.fetch_add(1, std::memory_order_relaxed) + 1;
                        uint64_t lastCapture = lastAutoBackgroundFrame_.load(std::memory_order_relaxed);
                        uint64_t framesSinceCapture = (idx > lastCapture) ? (idx - lastCapture) : 0;
                        
                        // Check if we should capture: enough consecutive empty frames AND cooldown period passed
                        if (currentEmpty >= static_cast<uint64_t>(config.auto_background_empty_frames) &&
                            framesSinceCapture >= static_cast<uint64_t>(config.auto_background_cooldown_frames)) {
                            
                            // Capture full frame as background (not just ROI)
                            cv::Mat fullGray = makeGrayCopy(f.width, f.height, f.linePitch, f.data.data());
                            if (!fullGray.empty()) {
                                setRealtimeBackgroundGray(fullGray);
                                lastAutoBackgroundFrame_.store(idx, std::memory_order_relaxed);
                                consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                                
                                // Update previous frame cache to current frame (for next frame-to-frame comparison)
                                {
                                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                                }
                                
                                // Notify via callback
                                {
                                    std::scoped_lock callbackLk(backgroundCaptureCallbackMutex_);
                                    if (backgroundCaptureCallback_) {
                                        backgroundCaptureCallback_(fullGray.clone(), idx);
                                    }
                                }
                                
                                SPDLOG_INFO("Auto-captured background at frame {} ({} consecutive empty frames)", 
                                           idx, currentEmpty);
                            }
                        }
                    } else {
                        // Reset counter if auto-capture disabled, experiment running, or movement detected
                        if (!config.auto_background_enabled || experimentActive_.load()) {
                            consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                        }
                    }
                    
                    rtLastProcessed_.store(idx);
                    continue;
                }
                
                // Reset counter on non-empty frames
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                }
                
                // Update previous frame for frame-to-frame comparison (always when auto-capture enabled)
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                }
                
                // Use background subtraction diff for actual processing (morphology, contours, etc.)
                cv::threshold(diffForProcessing, thresh, threshVal, 255, cv::THRESH_BINARY);
                
                // Update previous frame for frame-to-frame comparison (when no background and auto-capture enabled)
                if (!hasBackground && config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                }
                
                cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(morphK, morphK));
                cv::morphologyEx(thresh, roiDst, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), morphIter);
                cv::morphologyEx(roiDst, roiDst, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), morphIter);

                FilterResult validation = filterProcessedImage(mask, cvRoi, config, gray);
                
                // Extract contours from validation result for snapshot
                std::vector<std::vector<cv::Point>> contours = validation.allContours;
                const auto algoEnd = clock::now();
                const double algoMs = std::chrono::duration<double, std::milli>(algoEnd - algoStart).count();
                algoMsSinceSummary += algoMs;
                if (validation.isValid) {
                    ++validSinceSummary;
                } else {
                    ++invalidSinceSummary;
                }
                
                // Fire trigger + autofocus callbacks BEFORE taking monitoringFramesMutex_
                // so the UI thread's ring-buffer snapshot cannot stall the trigger path.
                // Target-group fires FIRST; RingRatio callback is O(1) on the realtime
                // thread as of 2026-04-16 (push into AutofocusService::pendingSamples_
                // + notify_one; the O(n log n) sort runs on AutofocusService::statsThread_).
                if (validation.isValid) {
                    TargetGroupCallback tgCb;
                    {
                        std::scoped_lock cbLk(targetGroupCallbackMutex_);
                        tgCb = targetGroupCallback_;
                    }
                    if (tgCb) tgCb(validation.isTargetGroup);

                    if (validation.ringRatio > 0.0) {
                        RingRatioCallback rrCb;
                        {
                            std::scoped_lock cbLk(ringRatioCallbackMutex_);
                            rrCb = ringRatioCallback_;
                        }
                        if (rrCb) rrCb(validation.ringRatio, f.timestamp);
                    }
                }

                // Always accumulate frames for monitoring (with size limit)
                {
                    ProcessedFrame monitoringFrame;
                    monitoringFrame.index = idx;
                    monitoringFrame.timestampNs = f.timestamp;
                    monitoringFrame.validation = validation;
                    // Store ROI-only images to reduce memory usage
                    cv::Mat roiOriginal = gray(cvRoi).clone();
                    cv::Mat roiMask = mask(cvRoi).clone();
                    monitoringFrame.originalImage = std::move(roiOriginal);
                    monitoringFrame.processedImage = std::move(roiMask);

                    std::scoped_lock monitoringLk(monitoringFramesMutex_);
                    if (validation.isValid) {
                        monitoringValidFrames_.push_back(std::move(monitoringFrame));
                    } else {
                        monitoringInvalidFrames_.push_back(std::move(monitoringFrame));
                    }

                    // Throttled DEBUG: accumulation sizes and process memory
                    if ((idx % 500ULL) == 0ULL) {
                        size_t vSz = 0;
                        size_t iSz = 0;
                        {
                            std::scoped_lock fLk(framesMutex_);
                            vSz = validFrames_.size();
                            iSz = invalidFrames_.size();
                        }
                        SPDLOG_DEBUG("Accumulated frames (idx={}): valid={}, invalid={}, flush_interval={}, since_last_flush={}, mem_mb={:.1f}",
                                     idx, vSz, iSz, flushInterval_.load(), framesSinceLastFlush_.load(), backend::Tools::getProcessMemoryMB());
                    }
                }

                // Throttled DEBUG: monitoring buffer sizes and process memory
                if ((idx % 500ULL) == 0ULL) {
                    size_t monValidSz = 0;
                    size_t monInvalidSz = 0;
                    {
                        std::scoped_lock mLk(monitoringFramesMutex_);
                        monValidSz = monitoringValidFrames_.size();
                        monInvalidSz = monitoringInvalidFrames_.size();
                    }
                    SPDLOG_DEBUG("Realtime monitoring sizes (idx={}): mon_valid={}, mon_invalid={}, mem_mb={:.1f}",
                                 idx, monValidSz, monInvalidSz, backend::Tools::getProcessMemoryMB());
                }
                
                // Also accumulate frames for experiment if active
                if (experimentActive_.load()) {
                    // Determine if we should save this frame
                    bool shouldSave = false;
                    if (validation.isValid) {
                        shouldSave = true;
                    } else {
                        size_t counter = invalidFrameCounter_.fetch_add(1, std::memory_order_relaxed);
                        size_t rate = invalidFrameSamplingRate_.load(std::memory_order_relaxed);
                        if (rate > 0 && (counter % rate) == 0) {
                            shouldSave = true;
                        }
                    }
                    
                    if (shouldSave) {
                        ProcessedFrame frame;
                        frame.index = idx;
                        frame.timestampNs = f.timestamp;
                        frame.validation = validation;
                        frame.originalImage = gray.clone();
                        frame.processedImage = mask.clone();
                        
                        appendExperimentFrame(std::move(frame), validation.isValid);
                    }
                }

                // Publish snapshot
                {
                    std::scoped_lock lk(snapshotMutex_);
                    latestSnapshot_.index = idx;
                    latestSnapshot_.mask = mask.clone();
                    latestSnapshot_.contours = std::move(contours);
                    latestSnapshot_.validation = validation;
                }

                rtLastProcessed_.store(idx);
            }

            // Per-frame timing
            const auto frameEnd = clock::now();
            const double ms = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
            SPDLOG_TRACE("Realtime processing: idx={} time_ms={:.3f} roi={}x{}", idx, ms, roi.w, roi.h);

            // Periodic summary
            framesSinceSummary += 1;
            msSinceSummary += ms;
            const auto now = frameEnd;
            const double windowMs = std::chrono::duration<double, std::milli>(now - lastSummaryTs).count();
            if (windowMs >= 1000.0) {
                const double avgMs = framesSinceSummary > 0 ? (msSinceSummary / static_cast<double>(framesSinceSummary)) : 0.0;
                const double algoAvgMs = framesSinceSummary > 0 ? (algoMsSinceSummary / static_cast<double>(framesSinceSummary)) : 0.0;
                const double fps = windowMs > 0.0 ? (static_cast<double>(framesSinceSummary) * 1000.0 / windowMs) : 0.0;
                const double vfps = windowMs > 0.0 ? (static_cast<double>(validSinceSummary) * 1000.0 / windowMs) : 0.0;
                const double ifps = windowMs > 0.0 ? (static_cast<double>(invalidSinceSummary) * 1000.0 / windowMs) : 0.0;
                algoFps1s_.store(fps, std::memory_order_relaxed);
                validFps1s_.store(vfps, std::memory_order_relaxed);
                invalidFps1s_.store(ifps, std::memory_order_relaxed);
                const double algoAvgUs = algoAvgMs * 1000.0;
                algoAvgUs1s_.store(algoAvgUs, std::memory_order_relaxed);
                algoAvgUs1sUpdatedUs_.store(backend::Tools::getTimestamp(), std::memory_order_relaxed);
                SPDLOG_DEBUG("Realtime processing summary: processed={} skipped={} window_ms={:.0f} avg_ms={:.3f} algo_avg_ms={:.3f} ~fps={:.1f}",
                             framesSinceSummary, framesSkippedSinceSummary, windowMs, avgMs, algoAvgMs, fps);

                // Extended summary: buffers, ROI, background, and process memory
                size_t vSz = 0, iSz = 0, monValidSz = 0, monInvalidSz = 0;
                {
                    std::scoped_lock fLk(framesMutex_);
                    vSz = validFrames_.size();
                    iSz = invalidFrames_.size();
                }
                {
                    std::scoped_lock mLk(monitoringFramesMutex_);
                    monValidSz = monitoringValidFrames_.size();
                    monInvalidSz = monitoringInvalidFrames_.size();
                }
                Roi roi{};
                bool hasBg = false;
                {
                    std::scoped_lock rtLk(rtMutex_);
                    roi = rtRoi_;
                    hasBg = (rtBgGray_ != nullptr && !rtBgGray_->empty());
                }
                const size_t flushInt = flushInterval_.load();
                const size_t sinceFlush = framesSinceLastFlush_.load();
                const double memMB = backend::Tools::getProcessMemoryMB();
                const double peakMB = backend::Tools::getPeakProcessMemoryMB();
                SPDLOG_DEBUG("Realtime buffers: acc_valid={} acc_invalid={} mon_valid={} mon_invalid={} flush_interval={} since_last_flush={} roi={}x{} bg={} mem_mb={:.1f} peak_mb={:.1f}",
                             vSz, iSz, monValidSz, monInvalidSz, flushInt, sinceFlush, roi.w, roi.h, hasBg ? 1 : 0, memMB, peakMB);
                lastSummaryTs = now;
                framesSinceSummary = 0;
                framesSkippedSinceSummary = 0;
                msSinceSummary = 0.0;
                algoMsSinceSummary = 0.0;
                validSinceSummary = 0;
                invalidSinceSummary = 0;
            }
        } else {
            for (uint64_t idx = last + 1; idx <= latest && rtRunning_.load(); ++idx) {
                const auto frameStart = clock::now();
                if (!rtEnabled_.load()) { rtLastProcessed_.store(idx); continue; }
                backend::playback::Frame f{};
                if (!rtStore_->getByWriteIndex(idx, f)) {
                    continue;
                }
                if (f.width == 0 || f.height == 0 || f.data.empty()) {
                    continue;
                }
                cv::Mat gray = makeGrayCopy(f.width, f.height, f.linePitch, f.data.data());

                // Grab ROI, background, and config first (outside frame processing to minimize lock time)
                Roi roi{};
                std::shared_ptr<cv::Mat> bgShared;
                ProcessingConfig config;
                {
                    std::scoped_lock lk(rtMutex_);
                    roi = rtRoi_;
                    bgShared = rtBgGray_; // shared_ptr copy is cheap, no cloning
                }
                {
                    std::scoped_lock cfgLk(configMutex_);
                    config = processingConfig_;
                }

                // Clamp ROI
                if (roi.w <= 0 || roi.h <= 0) {
                    roi.x = 0; roi.y = 0; roi.w = gray.cols; roi.h = gray.rows;
                }
                roi.x = std::max(0, std::min(roi.x, gray.cols - 1));
                roi.y = std::max(0, std::min(roi.y, gray.rows - 1));
                roi.w = std::max(1, std::min(roi.w, gray.cols - roi.x));
                roi.h = std::max(1, std::min(roi.h, gray.rows - roi.y));

                cv::Rect cvRoi(roi.x, roi.y, roi.w, roi.h);

                // Build full-size mask
                cv::Mat mask(gray.rows, gray.cols, CV_8UC1, cv::Scalar(0));
                cv::Mat roiCurr = gray(cvRoi);
                cv::Mat roiDst = mask(cvRoi);
                cv::Mat blurredCurr, blurredBg, thresh;
                const auto algoStart = clock::now();
                auto toOdd = [](int v) -> int { if (v < 1) v = 1; if ((v % 2) == 0) v += 1; return v; };
                const int blurK = toOdd(config.gaussian_blur_size);
                const int morphK = toOdd(config.morph_kernel_size);
                const int morphIter = std::max(1, config.morph_iterations);
                const int threshVal = std::max(0, config.bg_subtract_threshold);

                cv::GaussianBlur(roiCurr, blurredCurr, cv::Size(blurK, blurK), 0);
                bool hasBackground = (bgShared && !bgShared->empty() && bgShared->size() == gray.size() && bgShared->type() == CV_8UC1);
                
                // For processing: use background subtraction if available
                cv::Mat diffForProcessing;
                if (hasBackground) {
                    cv::GaussianBlur((*bgShared)(cvRoi), blurredBg, cv::Size(blurK, blurK), 0);
                    cv::subtract(blurredCurr, blurredBg, diffForProcessing);
                } else {
                    diffForProcessing = blurredCurr;
                }
                
                // For auto-capture detection: always use frame-to-frame difference when enabled
                cv::Mat diffForAutoCapture;
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    if (!previousFrameForAutoCapture_.empty() && 
                        previousFrameForAutoCapture_.size() == blurredCurr.size() &&
                        previousFrameForAutoCapture_.type() == blurredCurr.type()) {
                        cv::absdiff(blurredCurr, previousFrameForAutoCapture_, diffForAutoCapture);
                    } else {
                        // First frame or size mismatch: store current frame and skip auto-capture check
                        previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                        diffForAutoCapture = blurredCurr; // Use current frame for thresholding (will not be empty)
                    }
                } else {
                    diffForAutoCapture = diffForProcessing; // Fallback to processing diff
                }
                
                // Use frame-to-frame diff for empty frame detection when auto-capture is enabled
                cv::Mat diff = (config.auto_background_enabled && !experimentActive_.load()) ? diffForAutoCapture : diffForProcessing;
                cv::threshold(diff, thresh, threshVal, 255, cv::THRESH_BINARY);

                // Check for empty frame: count non-zero pixels after binary threshold
                int pixelCount = cv::countNonZero(thresh);
                if (pixelCount < config.empty_frame_pixel_threshold) {
                    SPDLOG_TRACE("Empty frame detected (idx={}, pixel_count={}, threshold={}), skipping further processing",
                                idx, pixelCount, config.empty_frame_pixel_threshold);
                    
                    // Auto-capture logic (only when experiment is NOT running)
                    if (config.auto_background_enabled && !experimentActive_.load()) {
                        uint64_t currentEmpty = consecutiveEmptyFrames_.fetch_add(1, std::memory_order_relaxed) + 1;
                        uint64_t lastCapture = lastAutoBackgroundFrame_.load(std::memory_order_relaxed);
                        uint64_t framesSinceCapture = (idx > lastCapture) ? (idx - lastCapture) : 0;
                        
                        // Check if we should capture: enough consecutive empty frames AND cooldown period passed
                        if (currentEmpty >= static_cast<uint64_t>(config.auto_background_empty_frames) &&
                            framesSinceCapture >= static_cast<uint64_t>(config.auto_background_cooldown_frames)) {
                            
                            // Capture full frame as background (not just ROI)
                            cv::Mat fullGray = makeGrayCopy(f.width, f.height, f.linePitch, f.data.data());
                            if (!fullGray.empty()) {
                                setRealtimeBackgroundGray(fullGray);
                                lastAutoBackgroundFrame_.store(idx, std::memory_order_relaxed);
                                consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                                
                                // Update previous frame cache to current frame (for next frame-to-frame comparison)
                                {
                                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                                }
                                
                                // Notify via callback
                                {
                                    std::scoped_lock callbackLk(backgroundCaptureCallbackMutex_);
                                    if (backgroundCaptureCallback_) {
                                        backgroundCaptureCallback_(fullGray.clone(), idx);
                                    }
                                }
                                
                                SPDLOG_INFO("Auto-captured background at frame {} ({} consecutive empty frames)", 
                                           idx, currentEmpty);
                            }
                        }
                    } else {
                        // Reset counter if auto-capture disabled, experiment running, or movement detected
                        if (!config.auto_background_enabled || experimentActive_.load()) {
                            consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                        }
                    }
                    
                    // Even on empty frames, capture series images if multi-image collection is active
                    if (multiImagePending && experimentActive_.load()) {
                        pendingMultiImageFrame.seriesImages.push_back(gray.clone());
                        --multiImageRemaining;
                        SPDLOG_TRACE("Multi-image series: captured empty frame {} (remaining={})", idx, multiImageRemaining);
                        if (multiImageRemaining == 0) {
                            multiImagePending = false;
                            SPDLOG_DEBUG("Multi-image series complete (with empty frames): trigger_idx={}, series_size={}",
                                        pendingMultiImageFrame.index, pendingMultiImageFrame.seriesImages.size());
                            appendExperimentFrame(std::move(pendingMultiImageFrame), true);
                            pendingMultiImageFrame = ProcessedFrame{};
                        }
                    }

                    rtLastProcessed_.store(idx);
                    continue; // Skip morphology, contours, validation, and frame accumulation
                }

                // Reset counter on non-empty frames
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    consecutiveEmptyFrames_.store(0, std::memory_order_relaxed);
                }

                // Update previous frame for frame-to-frame comparison (always when auto-capture enabled)
                if (config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                }

                // Use background subtraction diff for actual processing (morphology, contours, etc.)
                cv::threshold(diffForProcessing, thresh, threshVal, 255, cv::THRESH_BINARY);

                // Update previous frame for frame-to-frame comparison (when no background and auto-capture enabled)
                if (!hasBackground && config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                }

                cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(morphK, morphK));
                cv::morphologyEx(thresh, roiDst, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), morphIter);
                cv::morphologyEx(roiDst, roiDst, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), morphIter);

                // Always run validation for monitoring (even without experiment)
                // Use ROI-only data for validation (avoids O(frame_size) findContours/brightness scan)
                // mask is ROI-sized so contour coords are 0-based; use local roi for border check
                cv::Mat roiMaskForValidation = mask(cvRoi).clone();
                cv::Rect localRoi(0, 0, cvRoi.width, cvRoi.height);
                FilterResult validation = filterProcessedImage(roiMaskForValidation, localRoi, config, roiCurr);

                // Extract contours from validation result for snapshot
                // Contours are in ROI-relative coordinates — adjust to full-frame for snapshot/storage
                std::vector<std::vector<cv::Point>> contours = validation.allContours;
                for (auto& contour : contours) {
                    for (auto& pt : contour) {
                        pt.x += roi.x;
                        pt.y += roi.y;
                    }
                }
                const auto algoEnd = clock::now();
                const double algoMs = std::chrono::duration<double, std::milli>(algoEnd - algoStart).count();
                algoMsSinceSummary += algoMs;
                if (validation.isValid) {
                    ++validSinceSummary;
                } else {
                    ++invalidSinceSummary;
                }

                // Fire trigger + autofocus callbacks BEFORE taking monitoringFramesMutex_
                // so the UI thread's ring-buffer snapshot cannot stall the trigger path.
                // Target-group fires FIRST; RingRatio callback is O(1) on the realtime
                // thread as of 2026-04-16 (push into AutofocusService::pendingSamples_
                // + notify_one; the O(n log n) sort runs on AutofocusService::statsThread_).
                if (validation.isValid) {
                    TargetGroupCallback tgCb;
                    {
                        std::scoped_lock cbLk(targetGroupCallbackMutex_);
                        tgCb = targetGroupCallback_;
                    }
                    if (tgCb) tgCb(validation.isTargetGroup);

                    if (validation.ringRatio > 0.0) {
                        RingRatioCallback rrCb;
                        {
                            std::scoped_lock cbLk(ringRatioCallbackMutex_);
                            rrCb = ringRatioCallback_;
                        }
                        if (rrCb) rrCb(validation.ringRatio, f.timestamp);
                    }
                }

                // Always accumulate frames for monitoring (with size limit)
                {
                    ProcessedFrame monitoringFrame;
                    monitoringFrame.index = idx;
                    monitoringFrame.timestampNs = f.timestamp;
                    monitoringFrame.validation = validation;
                    // Store ROI-only images to reduce memory usage
                    cv::Mat roiOriginal = gray(cvRoi).clone();
                    cv::Mat roiMask = mask(cvRoi).clone();
                    monitoringFrame.originalImage = std::move(roiOriginal);
                    monitoringFrame.processedImage = std::move(roiMask);

                    std::scoped_lock monitoringLk(monitoringFramesMutex_);
                    if (validation.isValid) {
                        monitoringValidFrames_.push_back(std::move(monitoringFrame));
                    } else {
                        monitoringInvalidFrames_.push_back(std::move(monitoringFrame));
                    }

                    // Throttled TRACE: accumulation sizes and process memory
                    if ((idx % 5000ULL) == 0ULL) {
                        size_t vSz = 0;
                        size_t iSz = 0;
                        {
                            std::scoped_lock fLk(framesMutex_);
                            vSz = validFrames_.size();
                            iSz = invalidFrames_.size();
                        }
                        SPDLOG_TRACE("Accumulated frames (idx={}): valid={}, invalid={}, flush_interval={}, since_last_flush={}, mem_mb={:.1f}",
                                     idx, vSz, iSz, flushInterval_.load(), framesSinceLastFlush_.load(), backend::Tools::getProcessMemoryMB());
                    }
                }

                // Throttled TRACE: monitoring buffer sizes and process memory
                if ((idx % 5000ULL) == 0ULL) {
                    size_t monValidSz = 0;
                    size_t monInvalidSz = 0;
                    {
                        std::scoped_lock mLk(monitoringFramesMutex_);
                        monValidSz = monitoringValidFrames_.size();
                        monInvalidSz = monitoringInvalidFrames_.size();
                    }
                    SPDLOG_TRACE("Realtime monitoring sizes (idx={}): mon_valid={}, mon_invalid={}, mem_mb={:.1f}",
                                 idx, monValidSz, monInvalidSz, backend::Tools::getProcessMemoryMB());
                }

                // Also accumulate frames for experiment if active
                if (experimentActive_.load()) {
                    const bool multiImageMode = config.multi_image_enabled && config.multi_image_count > 1;

                    if (multiImagePending) {
                        // We're collecting series images for a pending multi-image trigger frame
                        pendingMultiImageFrame.seriesImages.push_back(gray.clone());
                        --multiImageRemaining;
                        SPDLOG_TRACE("Multi-image series: captured frame {} for series (remaining={})", idx, multiImageRemaining);

                        if (multiImageRemaining == 0) {
                            // Series complete — push to validFrames
                            multiImagePending = false;
                            SPDLOG_DEBUG("Multi-image series complete: trigger_idx={}, series_size={}",
                                        pendingMultiImageFrame.index, pendingMultiImageFrame.seriesImages.size());
                            appendExperimentFrame(std::move(pendingMultiImageFrame), true);
                            pendingMultiImageFrame = ProcessedFrame{}; // reset
                        }
                        // Skip normal valid/invalid save for this frame — it's part of the series
                    } else {
                        // Normal experiment accumulation (or start of new multi-image series)
                        bool shouldSave = false;
                        if (validation.isValid) {
                            if (multiImageMode) {
                                // Start a new multi-image series
                                pendingMultiImageFrame = ProcessedFrame{};
                                pendingMultiImageFrame.index = idx;
                                pendingMultiImageFrame.timestampNs = f.timestamp;
                                pendingMultiImageFrame.validation = validation;
                                pendingMultiImageFrame.originalImage = gray.clone();
                                pendingMultiImageFrame.processedImage = mask.clone();
                                pendingMultiImageFrame.seriesImages.push_back(gray.clone());
                                multiImageRemaining = static_cast<size_t>(config.multi_image_count - 1);
                                multiImagePending = true;
                                SPDLOG_DEBUG("Multi-image series started: trigger_idx={}, count={}", idx, config.multi_image_count);
                                // Don't save yet — wait for series to complete
                            } else {
                                shouldSave = true; // Normal single-image mode
                            }
                        } else {
                            // Sample invalid frames: save every Nth invalid frame
                            size_t counter = invalidFrameCounter_.fetch_add(1, std::memory_order_relaxed);
                            size_t rate = invalidFrameSamplingRate_.load(std::memory_order_relaxed);
                            if (rate > 0 && (counter % rate) == 0) {
                                shouldSave = true;
                            }
                        }

                        if (shouldSave) {
                            ProcessedFrame frame;
                            frame.index = idx;
                            frame.timestampNs = f.timestamp;
                            frame.validation = validation;
                            frame.originalImage = gray.clone();
                            frame.processedImage = mask.clone();

                            appendExperimentFrame(std::move(frame), validation.isValid);
                        }
                    }
                } else if (multiImagePending) {
                    // Experiment ended while collecting a multi-image series — save partial series
                    SPDLOG_WARN("Multi-image series incomplete (experiment ended): trigger_idx={}, collected={}/{}",
                                pendingMultiImageFrame.index, pendingMultiImageFrame.seriesImages.size(),
                                pendingMultiImageFrame.seriesImages.size() + multiImageRemaining);
                    multiImagePending = false;
                    multiImageRemaining = 0;
                    appendExperimentFrame(std::move(pendingMultiImageFrame), true);
                    pendingMultiImageFrame = ProcessedFrame{};
                }

                // Publish snapshot
                {
                    std::scoped_lock lk(snapshotMutex_);
                    latestSnapshot_.index = idx;
                    latestSnapshot_.mask = mask; // shallow copy ok; mask will be destroyed after leaving scope, so clone
                    latestSnapshot_.mask = latestSnapshot_.mask.clone();
                    latestSnapshot_.contours = std::move(contours);
                    latestSnapshot_.validation = validation;
                }

                rtLastProcessed_.store(idx);

                // Per-frame timing
                const auto frameEnd = clock::now();
                const double ms = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
                SPDLOG_TRACE("Realtime processing: idx={} time_ms={:.3f} roi={}x{}", idx, ms, roi.w, roi.h);

                // Periodic summary
                framesSinceSummary += 1;
                msSinceSummary += ms;
                const auto now = frameEnd;
                const double windowMs = std::chrono::duration<double, std::milli>(now - lastSummaryTs).count();
                if (windowMs >= 1000.0) {
                    const double avgMs = framesSinceSummary > 0 ? (msSinceSummary / static_cast<double>(framesSinceSummary)) : 0.0;
                    const double algoAvgMs = framesSinceSummary > 0 ? (algoMsSinceSummary / static_cast<double>(framesSinceSummary)) : 0.0;
                    const double fps = windowMs > 0.0 ? (static_cast<double>(framesSinceSummary) * 1000.0 / windowMs) : 0.0;
                    const double vfps = windowMs > 0.0 ? (static_cast<double>(validSinceSummary) * 1000.0 / windowMs) : 0.0;
                    const double ifps = windowMs > 0.0 ? (static_cast<double>(invalidSinceSummary) * 1000.0 / windowMs) : 0.0;
                    algoFps1s_.store(fps, std::memory_order_relaxed);
                    validFps1s_.store(vfps, std::memory_order_relaxed);
                    invalidFps1s_.store(ifps, std::memory_order_relaxed);
                    const double algoAvgUs = algoAvgMs * 1000.0;
                    algoAvgUs1s_.store(algoAvgUs, std::memory_order_relaxed);
                    algoAvgUs1sUpdatedUs_.store(backend::Tools::getTimestamp(), std::memory_order_relaxed);
                    SPDLOG_DEBUG("Realtime processing summary: processed={} skipped={} window_ms={:.0f} avg_ms={:.3f} algo_avg_ms={:.3f} ~fps={:.1f}",
                                 framesSinceSummary, framesSkippedSinceSummary, windowMs, avgMs, algoAvgMs, fps);

                    // Extended summary: buffers, ROI, background, and process memory
                    size_t vSz = 0, iSz = 0, monValidSz = 0, monInvalidSz = 0;
                    {
                        std::scoped_lock fLk(framesMutex_);
                        vSz = validFrames_.size();
                        iSz = invalidFrames_.size();
                    }
                    {
                        std::scoped_lock mLk(monitoringFramesMutex_);
                        monValidSz = monitoringValidFrames_.size();
                        monInvalidSz = monitoringInvalidFrames_.size();
                    }
                    Roi roi{};
                    bool hasBg = false;
                    {
                        std::scoped_lock rtLk(rtMutex_);
                        roi = rtRoi_;
                        hasBg = (rtBgGray_ != nullptr && !rtBgGray_->empty());
                    }
                    const size_t flushInt = flushInterval_.load();
                    const size_t sinceFlush = framesSinceLastFlush_.load();
                    const double memMB = backend::Tools::getProcessMemoryMB();
                    const double peakMB = backend::Tools::getPeakProcessMemoryMB();
                    SPDLOG_DEBUG("Realtime buffers: acc_valid={} acc_invalid={} mon_valid={} mon_invalid={} flush_interval={} since_last_flush={} roi={}x{} bg={} mem_mb={:.1f} peak_mb={:.1f}",
                                 vSz, iSz, monValidSz, monInvalidSz, flushInt, sinceFlush, roi.w, roi.h, hasBg ? 1 : 0, memMB, peakMB);
                    lastSummaryTs = now;
                    framesSinceSummary = 0;
                    framesSkippedSinceSummary = 0;
                    msSinceSummary = 0.0;
                    algoMsSinceSummary = 0.0;
                    validSinceSummary = 0;
                    invalidSinceSummary = 0;
                }
            }
        }
    }
}

} // namespace backend::services
