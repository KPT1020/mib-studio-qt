#include "backend/processing/ProcessingService.h"
#include "backend/recording/Hdf5Service.h"
#include "backend/diagnostics/CrashStateMirror.h"
#include "backend/playback/FrameStore.h"
#include "backend/app/Tools.h"

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

double rectArea(const cv::Rect2d& rect) {
    return std::max(0.0, rect.width) * std::max(0.0, rect.height);
}

double rectIou(const cv::Rect2d& a, const cv::Rect2d& b) {
    const double x1 = std::max(a.x, b.x);
    const double y1 = std::max(a.y, b.y);
    const double x2 = std::min(a.x + a.width, b.x + b.width);
    const double y2 = std::min(a.y + a.height, b.y + b.height);
    const double intersection = std::max(0.0, x2 - x1) * std::max(0.0, y2 - y1);
    const double unionArea = rectArea(a) + rectArea(b) - intersection;
    return unionArea > 0.0 ? intersection / unionArea : 0.0;
}

cv::Rect2d resultBbox(const FilterResult& result) {
    return cv::Rect2d(result.bboxX, result.bboxY, result.bboxWidth, result.bboxHeight);
}

void populateGeometry(FilterResult& result, const std::vector<cv::Point>& contour) {
    if (contour.empty()) {
        return;
    }

    const cv::Rect bbox = cv::boundingRect(contour);
    result.bboxX = static_cast<double>(bbox.x);
    result.bboxY = static_cast<double>(bbox.y);
    result.bboxWidth = static_cast<double>(bbox.width);
    result.bboxHeight = static_cast<double>(bbox.height);

    const cv::Moments moments = cv::moments(contour);
    if (std::abs(moments.m00) > std::numeric_limits<double>::epsilon()) {
        result.centroidX = moments.m10 / moments.m00;
        result.centroidY = moments.m01 / moments.m00;
    } else {
        result.centroidX = result.bboxX + result.bboxWidth * 0.5;
        result.centroidY = result.bboxY + result.bboxHeight * 0.5;
    }
}

struct BatchTrack {
    int id{-1};
    uint64_t firstFrame{0};
    uint64_t lastFrame{0};
    int observations{0};
    cv::Rect2d lastBbox;
    cv::Point2d lastCentroid;
    size_t outputIndex{0};
};

void applyTrackState(ProcessedFrame& frame, const BatchTrack& track) {
    frame.validation.trackId = track.id;
    frame.validation.trackFirstFrame = track.firstFrame;
    frame.validation.trackLastFrame = track.lastFrame;
    frame.validation.trackObservationCount = track.observations;
}

int findMatchingTrack(const std::vector<BatchTrack>& tracks,
                      const std::vector<bool>& matchedThisFrame,
                      const FilterResult& detection,
                      uint64_t frameIndex,
                      int frameWidth) {
    constexpr uint64_t kMaxFrameGap = 5;
    constexpr double kMinIou = 0.08;
    constexpr double kBaseCentroidThresholdPx = 24.0;
    constexpr double kMaxLeftwardJitterPx = 2.0;
    constexpr double kDirectionalFrameFraction = 0.35;
    constexpr double kMinDirectionalStepPx = 64.0;
    constexpr double kMinVerticalTolerancePx = 20.0;

    const cv::Rect2d bbox = resultBbox(detection);
    if (rectArea(bbox) <= 0.0) {
        return -1;
    }

    int bestTrack = -1;
    double bestScore = std::numeric_limits<double>::max();
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& track = tracks[i];
        if (i < matchedThisFrame.size() && matchedThisFrame[i]) {
            continue;
        }
        if (frameIndex <= track.lastFrame) {
            continue;
        }

        const uint64_t frameGap = frameIndex - track.lastFrame;
        if (frameGap > kMaxFrameGap) {
            continue;
        }

        if (detection.centroidX + kMaxLeftwardJitterPx < track.lastCentroid.x) {
            continue;
        }

        const double iou = rectIou(bbox, track.lastBbox);
        const double dx = detection.centroidX - track.lastCentroid.x;
        const double dy = std::abs(detection.centroidY - track.lastCentroid.y);
        const double motionThreshold = std::max(
            kBaseCentroidThresholdPx,
            std::max(track.lastBbox.width, track.lastBbox.height) * 1.25 +
                static_cast<double>(frameGap) * 8.0);
        const double directionalStep = std::max(
            motionThreshold,
            std::max(kMinDirectionalStepPx,
                     static_cast<double>(std::max(1, frameWidth)) * kDirectionalFrameFraction) *
                static_cast<double>(frameGap));
        const double verticalTolerance = std::max(
            kMinVerticalTolerancePx,
            std::max(track.lastBbox.height, bbox.height) * 1.5);
        if (iou < kMinIou && (dx > directionalStep || dy > verticalTolerance)) {
            continue;
        }

        const double score = (1.0 - std::min(1.0, iou)) +
                             (std::max(0.0, dx) / std::max(1.0, directionalStep)) +
                             (dy / std::max(1.0, verticalTolerance)) +
                             static_cast<double>(frameGap) * 0.05;
        if (score < bestScore) {
            bestScore = score;
            bestTrack = static_cast<int>(i);
        }
    }
    return bestTrack;
}

} // namespace

ProcessingService::ProcessingService() = default;
ProcessingService::~ProcessingService() {
    // A joinable realtimeThread_ at destruction would std::terminate; do not
    // rely on the GUI teardown path having called stopRealtime() first.
    stopRealtime();
    stopBatchPipeline();
    stop();
}

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
            try {
                job();
            } catch (const std::exception& ex) {
                SPDLOG_ERROR("ProcessingService: worker job threw: {} — job skipped", ex.what());
            } catch (...) {
                SPDLOG_ERROR("ProcessingService: worker job threw unknown exception — job skipped");
            }
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

void ProcessingService::setRealtimeProcessingMode(RealtimeProcessingMode mode) {
    const int normalized = static_cast<int>(mode);
    const int previous = rtProcessingMode_.exchange(normalized, std::memory_order_acq_rel);
    if (previous == normalized) {
        return;
    }

    SPDLOG_INFO("ProcessingService: realtime processing mode set to {}",
                mode == RealtimeProcessingMode::AsyncBatch ? "async_batch" : "inline");

    if (rtRunning_.load(std::memory_order_acquire)) {
        auto store = rtStore_;
        stopRealtime();
        startRealtime(store);
    }
}

ProcessingService::RealtimeProcessingMode ProcessingService::getRealtimeProcessingMode() const {
    const int mode = rtProcessingMode_.load(std::memory_order_acquire);
    return mode == static_cast<int>(RealtimeProcessingMode::AsyncBatch)
               ? RealtimeProcessingMode::AsyncBatch
               : RealtimeProcessingMode::Inline;
}

void ProcessingService::setRealtimeBatchSettings(const RealtimeBatchSettings& settings) {
    RealtimeBatchSettings normalized = settings;
    normalized.batchSize = std::max<size_t>(1, normalized.batchSize);
    normalized.maxQueuedFrames = std::max(normalized.batchSize, normalized.maxQueuedFrames);
    normalized.workerCount = std::max<size_t>(1, normalized.workerCount);
    normalized.maxBatchDelayMs = std::max(1, normalized.maxBatchDelayMs);

    bool restart = false;
    {
        std::scoped_lock lk(rtBatchSettingsMutex_);
        restart = rtRunning_.load(std::memory_order_acquire) &&
                  getRealtimeProcessingMode() == RealtimeProcessingMode::AsyncBatch &&
                  rtBatchSettings_.workerCount != normalized.workerCount;
        rtBatchSettings_ = normalized;
    }

    SPDLOG_INFO("ProcessingService: realtime batch settings batch_size={}, max_queue={}, workers={}, max_delay_ms={}",
                normalized.batchSize, normalized.maxQueuedFrames, normalized.workerCount, normalized.maxBatchDelayMs);

    if (restart) {
        auto store = rtStore_;
        stopRealtime();
        startRealtime(store);
    } else {
        refreshRealtimeBatchPipelineConfig();
    }
}

ProcessingService::RealtimeBatchSettings ProcessingService::getRealtimeBatchSettings() const {
    std::scoped_lock lk(rtBatchSettingsMutex_);
    return rtBatchSettings_;
}

void ProcessingService::setRealtimeRoi(const Roi& roi) {
    {
        std::scoped_lock lk(rtMutex_);
        rtRoi_ = roi;
    }
    configVersion_.fetch_add(1, std::memory_order_release);
    refreshRealtimeBatchPipelineConfig();
}

ProcessingService::Roi ProcessingService::getRealtimeRoi() const {
    std::scoped_lock lk(rtMutex_);
    return rtRoi_;
}

void ProcessingService::setRealtimeBackgroundGray(const cv::Mat& bg) {
    {
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
    configVersion_.fetch_add(1, std::memory_order_release); // wake cached-config refresh in realtime loop
    refreshRealtimeBatchPipelineConfig();
}

cv::Mat ProcessingService::getRealtimeBackgroundGray() const {
    std::scoped_lock lk(rtMutex_);
    if (rtBgGray_ && !rtBgGray_->empty()) {
        return rtBgGray_->clone();
    }
    return cv::Mat();
}

std::shared_ptr<const cv::Mat> ProcessingService::getRealtimeBackgroundGrayShared() const {
    std::scoped_lock lk(rtMutex_);
    return rtBgGray_; // implicit conversion shared_ptr<cv::Mat> → shared_ptr<const cv::Mat>
}

uint64_t ProcessingService::getConfigVersion() const {
    return configVersion_.load(std::memory_order_acquire);
}

bool ProcessingService::getLatestSnapshot(RealtimeSnapshot& out) {
    std::shared_ptr<const RealtimeSnapshot> snap;
    {
        std::scoped_lock lk(snapshotMutex_);
        snap = latestSnapshot_; // O(1) pointer copy inside lock
    }
    if (!snap || (snap->mask.empty() && snap->contours.empty())) return false;
    out.index = snap->index;
    out.mask = snap->mask;           // shallow refcount share (read-only consumers)
    out.contours = snap->contours;
    out.validation = snap->validation;
    return true;
}

void ProcessingService::startExperiment() {
    {
        // Tear down any prior flush queue (dtor drains + joins) so a new
        // experiment starts with a clean, non-errored writer.
        std::scoped_lock qlk(flushQueueMutex_);
        flushQueue_.reset();
    }
    std::scoped_lock lk(framesMutex_);
    validFrames_.clear();
    invalidFrames_.clear();
    const size_t flushInterval = flushInterval_.load(std::memory_order_relaxed);
    const size_t maxBuffered = maxBufferedFrames_.load(std::memory_order_relaxed);
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
    return {validFrames_.begin(), validFrames_.end()};
}

std::vector<ProcessedFrame> ProcessingService::getInvalidFrames() const {
    std::scoped_lock lk(framesMutex_);
    return {invalidFrames_.begin(), invalidFrames_.end()};
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

void ProcessingService::setMonitoringActive(bool active) {
    monitoringActive_.store(active, std::memory_order_relaxed);
}

void ProcessingService::setProcessingConfig(const ProcessingConfig& config) {
    {
        std::scoped_lock lk(configMutex_);
        processingConfig_ = config;
    }
    configVersion_.fetch_add(1, std::memory_order_release);
    refreshRealtimeBatchPipelineConfig();
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

static inline cv::Mat makeGrayCopy(const backend::playback::Frame& frame) {
    if (frame.data.empty() || frame.width == 0 || frame.height == 0) {
        return cv::Mat();
    }
    const int w = static_cast<int>(frame.width);
    const int h = static_cast<int>(frame.height);
    const size_t step = (frame.linePitch == 0 ? static_cast<size_t>(frame.width) : frame.linePitch);
    // Geometry comes from the camera and the buffer from a separate SDK
    // query; a short buffer (pixel-format change, partial delivery on
    // disconnect) would make the clone below read out of bounds.
    const size_t requiredBytes = static_cast<size_t>(h - 1) * step + static_cast<size_t>(w);
    if (frame.data.size() < requiredBytes) {
        SPDLOG_WARN("ProcessingService: frame data ({} bytes) smaller than geometry requires "
                    "({}x{} pitch={} -> {} bytes); frame skipped",
                    frame.data.size(), frame.width, frame.height, step, requiredBytes);
        return cv::Mat();
    }
    cv::Mat view(h, w, CV_8UC1, const_cast<uint8_t*>(frame.data.data()), step);
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
    // Same producer-trust issue as makeGrayCopy: validate the buffer actually
    // covers the rows the strided view will touch.
    const size_t requiredBytes = static_cast<size_t>(clampedY + clampedH - 1) * srcPitch +
                                 static_cast<size_t>(clampedX + clampedW);
    if (frame.data.size() < requiredBytes) {
        SPDLOG_WARN("ProcessingService: frame data ({} bytes) smaller than ROI requires "
                    "({}x{} pitch={} -> {} bytes); frame skipped",
                    frame.data.size(), frame.width, frame.height, srcPitch, requiredBytes);
        return cv::Mat();
    }
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

    cv::Mat gray = makeGrayCopy(frame);
    if (gray.empty()) {
        return true; // undecodable frame counts as empty
    }

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

bool ProcessingService::isFrameEmpty(const backend::playback::Frame& frame,
                                     const ProcessingConfig& config,
                                     const Roi& roi,
                                     const std::shared_ptr<const cv::Mat>& background) {
    if (frame.width == 0 || frame.height == 0 || frame.data.empty()) {
        return true;
    }

    const int frameW = static_cast<int>(frame.width);
    const int frameH = static_cast<int>(frame.height);

    Roi effectiveRoi = roi;
    if (effectiveRoi.w <= 0 || effectiveRoi.h <= 0) {
        effectiveRoi = {0, 0, frameW, frameH};
    }
    effectiveRoi.x = std::max(0, std::min(effectiveRoi.x, frameW - 1));
    effectiveRoi.y = std::max(0, std::min(effectiveRoi.y, frameH - 1));
    effectiveRoi.w = std::max(1, std::min(effectiveRoi.w, frameW - effectiveRoi.x));
    effectiveRoi.h = std::max(1, std::min(effectiveRoi.h, frameH - effectiveRoi.y));

    // ROI-only extraction — no full-frame copy
    cv::Mat roiCurr = makeGrayROI(frame, effectiveRoi.x, effectiveRoi.y, effectiveRoi.w, effectiveRoi.h);

    cv::Rect cvRoi(effectiveRoi.x, effectiveRoi.y, effectiveRoi.w, effectiveRoi.h);
    cv::Mat blurredCurr, blurredBg, diff, thresh;
    cv::GaussianBlur(roiCurr, blurredCurr, cv::Size(3, 3), 0);

    if (background && !background->empty() &&
        background->cols == frameW && background->rows == frameH &&
        background->type() == CV_8UC1) {
        cv::GaussianBlur((*background)(cvRoi), blurredBg, cv::Size(3, 3), 0);
        cv::subtract(blurredCurr, blurredBg, diff);
    } else {
        diff = blurredCurr;
    }

    cv::threshold(diff, thresh, config.bg_subtract_threshold, 255, cv::THRESH_BINARY);
    return cv::countNonZero(thresh) < config.empty_frame_pixel_threshold;
}

void ProcessingService::applyProcessingThreshold(const cv::Mat& diff, cv::Mat& thresh,
                                                  const ProcessingConfig& config) {
    const int fixedT = std::max(0, config.bg_subtract_threshold);
    if (!config.adaptive_threshold) {
        cv::threshold(diff, thresh, fixedT, 255, cv::THRESH_BINARY);
        return;
    }
    // Otsu picks a per-frame threshold from the diff histogram. Floor it at the
    // fixed noise threshold so a near-empty ROI (unimodal, near-zero diff) can't
    // be split into false foreground — on such frames Otsu returns a low value
    // that the floor overrides, reproducing the fixed-threshold behaviour. A
    // genuine bright cell yields a bimodal histogram whose Otsu split sits above
    // the floor and tightly segments the object. otsu_scale tunes mask tightness.
    // Requires a background-subtracted diff (bright object on ~0 field).
    cv::Mat otsuMask;
    const double otsuT = cv::threshold(diff, otsuMask, 0, 255,
                                       cv::THRESH_BINARY | cv::THRESH_OTSU);
    const double t = std::max(static_cast<double>(fixedT), otsuT * config.otsu_scale);
    if (t <= static_cast<double>(fixedT)) {
        // Otsu did not clear the floor: the floored binary is exactly otsuMask
        // re-thresholded at fixedT — compute directly to avoid a second pass only
        // when it differs.
        cv::threshold(diff, thresh, fixedT, 255, cv::THRESH_BINARY);
    } else {
        cv::threshold(diff, thresh, t, 255, cv::THRESH_BINARY);
    }
}

cv::Mat ProcessingService::buildMeasurementMask(const cv::Mat& diff, const cv::Size& maskSize,
                                                const cv::Rect& morphRegion, int morphK, int morphIter,
                                                const ProcessingConfig& config) const {
    // Only needed when detection runs adaptively: with the fixed threshold the
    // detection mask already IS the measurement mask, so callers measure on it.
    if (!config.adaptive_threshold) {
        return cv::Mat();
    }
    cv::Mat measMask = cv::Mat::zeros(maskSize, CV_8UC1);
    cv::Mat measThresh;
    cv::threshold(diff, measThresh, std::max(0, config.bg_subtract_threshold), 255, cv::THRESH_BINARY);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(morphK, morphK));
    cv::Mat dst = measMask(morphRegion);
    cv::morphologyEx(measThresh, dst, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), morphIter);
    cv::morphologyEx(dst, dst, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), morphIter);
    return measMask;
}

int ProcessingService::matchContourByOverlap(const std::vector<std::vector<cv::Point>>& candidates,
                                             const std::vector<cv::Point>& object,
                                             const cv::Size& size, double minIoM) {
    if (object.size() < 3 || candidates.empty()) {
        return -1;
    }
    const cv::Rect full(0, 0, size.width, size.height);
    const cv::Rect objBox = cv::boundingRect(object) & full;
    if (objBox.area() <= 0) {
        return -1;
    }
    // Rasterize the detected object's footprint once, in its own bbox frame.
    // Every candidate is drawn into the same small frame so intersection is a
    // cheap countNonZero on objBox-sized scratch masks, not the whole image.
    const cv::Point off = objBox.tl();
    cv::Mat objMask = cv::Mat::zeros(objBox.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> shifted(1);
    shifted[0].reserve(object.size());
    for (const auto& p : object) shifted[0].push_back(p - off);
    cv::drawContours(objMask, shifted, 0, cv::Scalar(255), cv::FILLED);
    const int objArea = cv::countNonZero(objMask);
    if (objArea <= 0) {
        return -1;
    }

    int best = -1;
    double bestIoM = 0.0;
    cv::Mat candMask(objBox.size(), CV_8UC1);
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        if (c.size() < 3) {
            continue;
        }
        if ((cv::boundingRect(c) & objBox).area() <= 0) {
            continue;  // bboxes disjoint -> no overlap
        }
        candMask.setTo(cv::Scalar(0));
        shifted[0].clear();
        shifted[0].reserve(c.size());
        for (const auto& p : c) shifted[0].push_back(p - off);
        cv::drawContours(candMask, shifted, 0, cv::Scalar(255), cv::FILLED);
        const int inter = cv::countNonZero(objMask & candMask);
        if (inter == 0) {
            continue;
        }
        // Denominator is the smaller of the two full areas (intersection-over-
        // min), so a fragment that sits fully inside the object still scores high.
        const double denom = std::min(static_cast<double>(objArea), cv::contourArea(c));
        const double iom = denom > 0.0 ? inter / denom : 0.0;
        if (iom > bestIoM) {
            bestIoM = iom;
            best = static_cast<int>(i);
        }
    }
    return bestIoM >= minIoM ? best : -1;
}

void ProcessingService::computeFocusMetrics(const cv::Mat& originalImage, const cv::Mat& objectMask,
                                            const cv::Rect& bbox, double& lapVar, double& tenengrad) {
    lapVar = 0.0;
    tenengrad = 0.0;
    if (originalImage.empty() || objectMask.empty()) {
        return;
    }
    // Work on the object's bounding box only. Clamp to the shared image bounds.
    cv::Rect area(0, 0, std::min(originalImage.cols, objectMask.cols),
                  std::min(originalImage.rows, objectMask.rows));
    if (bbox.width > 0 && bbox.height > 0) {
        area &= bbox;
    }
    if (area.width < 3 || area.height < 3) {
        return;  // too small for a 3x3 derivative
    }
    cv::Mat gray = originalImage(area);
    if (gray.channels() == 3) {
        cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
    }
    const cv::Mat mask = objectMask(area);

    // Laplacian and Sobel are high-pass, so the smooth per-row background barely
    // contributes — computing on the original intensity is equivalent to the
    // background-subtracted diff (Spearman ~0.91) without needing the diff here.
    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_32F, 3);
    cv::Scalar m, sd;
    cv::meanStdDev(lap, m, sd, mask);
    lapVar = sd[0] * sd[0];  // variance of the Laplacian within the mask

    cv::Mat gx, gy;
    cv::Sobel(gray, gx, CV_32F, 1, 0, 3);
    cv::Sobel(gray, gy, CV_32F, 0, 1, 3);
    cv::Mat energy;
    cv::magnitude(gx, gy, energy);
    cv::multiply(energy, energy, energy);  // squared gradient magnitude
    tenengrad = cv::mean(energy, mask)[0];
}

ProcessedFrame ProcessingService::computeProcessedFrame(
    const cv::Mat& grayInput,
    const cv::Mat& backgroundGray,
    const ProcessingConfig& config,
    const Roi& roiIn,
    uint64_t index,
    uint64_t timestampNs,
    cv::Mat* measurementMaskOut) {

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
        // Proposed pipeline: |cur - bg| captures the whole cell, not just the
        // brighter-than-background part that signed subtract keeps.
        if (config.proposed_pipeline) {
            cv::absdiff(blurredCurr, blurredBg, diffForProcessing);
        } else {
            cv::subtract(blurredCurr, blurredBg, diffForProcessing);
        }
    } else {
        diffForProcessing = blurredCurr;
    }

    cv::Mat kernel;
    if (config.proposed_pipeline) {
        // Optional white top-hat re-flattens residual shading before Otsu (off
        // by default — absdiff alone carries the accuracy win on flat fields).
        if (config.proposed_tophat_kernel > 1) {
            const int tk = config.proposed_tophat_kernel | 1;  // force odd
            const cv::Mat thk = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(tk, tk));
            cv::morphologyEx(diffForProcessing, diffForProcessing, cv::MORPH_TOPHAT, thk);
        }
        // Proposed always uses the per-frame Otsu cut regardless of the flag.
        ProcessingConfig otsuCfg = config;
        otsuCfg.adaptive_threshold = true;
        applyProcessingThreshold(diffForProcessing, thresh, otsuCfg);
        kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morphK, morphK));
    } else {
        applyProcessingThreshold(diffForProcessing, thresh, config);
        kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(morphK, morphK));
    }
    cv::morphologyEx(thresh, roiDst, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), morphIter);
    cv::morphologyEx(roiDst, roiDst, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), morphIter);

    // Fixed-threshold measurement mask (empty unless adaptive detection is on) so
    // size metrics do not drift with the per-frame Otsu cut. The proposed
    // pipeline's mask is already accurate, so it measures on itself (no decouple).
    cv::Mat measMask;
    if (!config.proposed_pipeline) {
        measMask = buildMeasurementMask(diffForProcessing, mask.size(), cvRoi, morphK, morphIter, config);
    }
    if (measurementMaskOut) {
        *measurementMaskOut = measMask;
    }

    // Validation + contour/metric extraction (same helper as realtime)
    out.validation = filterProcessedImage(mask, cvRoi, config, gray, measMask);
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
    std::vector<BatchTrack> tracks;
    int nextTrackId = 1;

    const size_t total = grayImages.size();
    if (progress) progress(BatchProgress{0, total});

    for (size_t i = 0; i < total; ++i) {
        cv::Mat measMask;  // fixed-threshold measurement mask (empty unless adaptive)
        ProcessedFrame base = computeProcessedFrame(grayImages[i], background, config, roi,
                                                    static_cast<uint64_t>(i), 0, &measMask);
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

        auto objectResults = filterProcessedObjects(base.processedImage, cvRoi, config, base.originalImage, measMask);
        if (objectResults.empty()) {
            results.emplace_back(std::move(base));
        } else {
            std::vector<bool> matchedThisFrame(tracks.size(), false);
            for (auto& validation : objectResults) {
                ProcessedFrame objectFrame;
                objectFrame.index = base.index;
                objectFrame.timestampNs = base.timestampNs;
                objectFrame.originalImage = base.originalImage.clone();
                objectFrame.processedImage = base.processedImage.clone();
                objectFrame.validation = std::move(validation);
                if (!objectFrame.validation.isValid) {
                    results.emplace_back(std::move(objectFrame));
                    continue;
                }

                const int trackIdx = findMatchingTrack(
                    tracks, matchedThisFrame, objectFrame.validation, objectFrame.index, cvRoi.width);
                if (trackIdx >= 0) {
                    auto& track = tracks[static_cast<size_t>(trackIdx)];
                    track.lastFrame = objectFrame.index;
                    track.lastBbox = resultBbox(objectFrame.validation);
                    track.lastCentroid = cv::Point2d(objectFrame.validation.centroidX,
                                                     objectFrame.validation.centroidY);
                    ++track.observations;
                    if (static_cast<size_t>(trackIdx) >= matchedThisFrame.size()) {
                        matchedThisFrame.resize(tracks.size(), false);
                    }
                    matchedThisFrame[static_cast<size_t>(trackIdx)] = true;
                    applyTrackState(results[track.outputIndex], track);
                    continue;
                }

                BatchTrack track;
                track.id = nextTrackId++;
                track.firstFrame = objectFrame.index;
                track.lastFrame = objectFrame.index;
                track.observations = 1;
                track.lastBbox = resultBbox(objectFrame.validation);
                track.lastCentroid = cv::Point2d(objectFrame.validation.centroidX,
                                                 objectFrame.validation.centroidY);
                track.outputIndex = results.size();
                applyTrackState(objectFrame, track);
                results.emplace_back(std::move(objectFrame));
                tracks.push_back(std::move(track));
                matchedThisFrame.push_back(true);
            }
        }
        if (progress) progress(BatchProgress{i + 1, total});
    }

    SPDLOG_INFO("processBatch: processed {} images into {} records across {} tracks (roi={}x{} at {},{}, background={})",
                total, results.size(), tracks.size(), roi.w, roi.h, roi.x, roi.y, !background.empty());
    return results;
}

bool ProcessingService::startBatchPipeline(BatchPipelineConfig config, BatchResultCallback callback) {
    if (batchRunning_.load(std::memory_order_acquire)) {
        SPDLOG_WARN("Batch pipeline already running");
        return false;
    }

    if (config.batchSize == 0) {
        config.batchSize = 1;
    }
    if (config.maxQueuedFrames == 0) {
        config.maxQueuedFrames = config.batchSize;
    }
    if (config.workerCount == 0) {
        config.workerCount = 1;
    }
    if (config.maxBatchDelayMs <= 0) {
        config.maxBatchDelayMs = 1;
    }

    {
        std::scoped_lock lk(batchMutex_);
        std::queue<QueuedBatchFrame> empty;
        batchQueue_.swap(empty);
        batchConfig_ = std::move(config);
        batchResultCallback_ = std::move(callback);
        batchFramesAccepted_.store(0, std::memory_order_relaxed);
        batchFramesDropped_.store(0, std::memory_order_relaxed);
        batchFramesProcessed_.store(0, std::memory_order_relaxed);
        batchBatchesProcessed_.store(0, std::memory_order_relaxed);
        batchAlgoMicrosTotal_.store(0, std::memory_order_relaxed);
        batchMaxQueueDepth_.store(0, std::memory_order_relaxed);
        batchWorkerCount_.store(batchConfig_.workerCount, std::memory_order_relaxed);
    }

    batchRunning_.store(true, std::memory_order_release);
    batchWorkers_.reserve(batchWorkerCount_.load(std::memory_order_relaxed));
    for (size_t i = 0; i < batchWorkerCount_.load(std::memory_order_relaxed); ++i) {
        batchWorkers_.emplace_back(&ProcessingService::batchWorkerLoop, this);
    }

    SPDLOG_INFO("Batch pipeline started: batch_size={}, max_queue={}, workers={}, max_delay_ms={}",
                batchConfig_.batchSize, batchConfig_.maxQueuedFrames, batchConfig_.workerCount,
                batchConfig_.maxBatchDelayMs);
    return true;
}

void ProcessingService::stopBatchPipeline() {
    if (!batchRunning_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    batchCv_.notify_all();
    for (auto& worker : batchWorkers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    batchWorkers_.clear();

    {
        std::scoped_lock lk(batchMutex_);
        std::queue<QueuedBatchFrame> empty;
        batchQueue_.swap(empty);
        batchResultCallback_ = {};
        batchWorkerCount_.store(0, std::memory_order_relaxed);
    }

    SPDLOG_INFO("Batch pipeline stopped: accepted={}, processed={}, dropped={}, batches={}",
                batchFramesAccepted_.load(std::memory_order_relaxed),
                batchFramesProcessed_.load(std::memory_order_relaxed),
                batchFramesDropped_.load(std::memory_order_relaxed),
                batchBatchesProcessed_.load(std::memory_order_relaxed));
}

bool ProcessingService::enqueueBatchFrame(const cv::Mat& grayImage, uint64_t index, uint64_t timestampNs) {
    if (!batchRunning_.load(std::memory_order_acquire) || grayImage.empty()) {
        return false;
    }

    cv::Mat gray;
    if (grayImage.type() == CV_8UC1) {
        gray = grayImage.clone();
    } else if (grayImage.channels() == 3) {
        cv::cvtColor(grayImage, gray, cv::COLOR_BGR2GRAY);
    } else {
        grayImage.convertTo(gray, CV_8UC1);
    }

    bool shouldNotify = false;
    {
        std::scoped_lock lk(batchMutex_);
        if (!batchRunning_.load(std::memory_order_relaxed)) {
            return false;
        }
        if (batchQueue_.size() >= batchConfig_.maxQueuedFrames) {
            batchFramesDropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        batchQueue_.push(QueuedBatchFrame{std::move(gray), index, timestampNs});
        batchFramesAccepted_.fetch_add(1, std::memory_order_relaxed);

        const size_t depth = batchQueue_.size();
        size_t observed = batchMaxQueueDepth_.load(std::memory_order_relaxed);
        while (depth > observed &&
               !batchMaxQueueDepth_.compare_exchange_weak(observed, depth, std::memory_order_relaxed)) {
        }

        shouldNotify = depth >= batchConfig_.batchSize;
    }

    if (shouldNotify) {
        batchCv_.notify_one();
    }
    return true;
}

bool ProcessingService::enqueueBatchFrame(const backend::playback::Frame& frame, uint64_t index) {
    if (frame.width == 0 || frame.height == 0 || frame.data.empty()) {
        return false;
    }

    cv::Mat gray = makeGrayCopy(frame);
    if (gray.empty()) {
        return false;
    }
    return enqueueBatchFrame(gray, index, frame.timestamp);
}

ProcessingService::BatchPipelineStats ProcessingService::getBatchPipelineStats() const {
    BatchPipelineStats stats;
    stats.framesAccepted = batchFramesAccepted_.load(std::memory_order_relaxed);
    stats.framesDropped = batchFramesDropped_.load(std::memory_order_relaxed);
    stats.framesProcessed = batchFramesProcessed_.load(std::memory_order_relaxed);
    stats.batchesProcessed = batchBatchesProcessed_.load(std::memory_order_relaxed);
    stats.maxQueueDepth = batchMaxQueueDepth_.load(std::memory_order_relaxed);
    stats.workerCount = batchWorkerCount_.load(std::memory_order_relaxed);
    stats.running = batchRunning_.load(std::memory_order_acquire);

    std::scoped_lock lk(batchMutex_);
    stats.currentQueueDepth = batchQueue_.size();
    stats.batchSize = batchConfig_.batchSize;
    if (stats.workerCount == 0 && stats.running) {
        stats.workerCount = batchConfig_.workerCount;
    }
    return stats;
}

void ProcessingService::batchWorkerLoop() {
    while (true) {
        std::vector<QueuedBatchFrame> inputs;
        BatchPipelineConfig config;
        BatchResultCallback callback;

        {
            std::unique_lock lk(batchMutex_);
            batchCv_.wait_for(lk,
                              std::chrono::milliseconds(std::max(1, batchConfig_.maxBatchDelayMs)),
                              [&] {
                                  const size_t batchSize = std::max<size_t>(1, batchConfig_.batchSize);
                                  return !batchRunning_.load(std::memory_order_acquire) ||
                                         batchQueue_.size() >= batchSize;
                              });

            if (batchQueue_.empty() && !batchRunning_.load(std::memory_order_acquire)) {
                break;
            }
            if (batchQueue_.empty()) {
                continue;
            }

            const size_t batchSize = std::max<size_t>(1, batchConfig_.batchSize);
            const size_t desired = std::min(batchQueue_.size(), batchSize);

            inputs.reserve(desired);
            for (size_t i = 0; i < desired && !batchQueue_.empty(); ++i) {
                inputs.emplace_back(std::move(batchQueue_.front()));
                batchQueue_.pop();
            }
            config = batchConfig_;
            callback = batchResultCallback_;
        }

        const auto algoStart = std::chrono::steady_clock::now();
        std::vector<ProcessedFrame> results;
        results.reserve(inputs.size());
        try {
        for (const auto& item : inputs) {
            cv::Mat measMask;  // fixed-threshold measurement mask (empty unless adaptive)
            ProcessedFrame base = computeProcessedFrame(item.gray,
                                                        config.background,
                                                        config.processing,
                                                        config.roi,
                                                        item.index,
                                                        item.timestampNs,
                                                        &measMask);
            if (base.originalImage.empty() || base.processedImage.empty()) {
                results.emplace_back(std::move(base));
                continue;
            }

            Roi normalizedRoi = config.roi;
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

            auto objectResults = filterProcessedObjects(base.processedImage, cvRoi, config.processing, base.originalImage, measMask);
            if (objectResults.empty()) {
                results.emplace_back(std::move(base));
                continue;
            }

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
        const auto algoEnd = std::chrono::steady_clock::now();
        const auto algoMicros = std::chrono::duration_cast<std::chrono::microseconds>(algoEnd - algoStart).count();
        if (algoMicros > 0) {
            batchAlgoMicrosTotal_.fetch_add(static_cast<uint64_t>(algoMicros), std::memory_order_relaxed);
        }

        if (!results.empty()) {
            batchFramesProcessed_.fetch_add(static_cast<uint64_t>(inputs.size()), std::memory_order_relaxed);
            batchBatchesProcessed_.fetch_add(1, std::memory_order_relaxed);
            if (callback) {
                callback(std::move(results));
            }
        }
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("ProcessingService: batch worker exception: {} — batch of {} frames dropped",
                         ex.what(), inputs.size());
        } catch (...) {
            SPDLOG_ERROR("ProcessingService: batch worker unknown exception — batch of {} frames dropped",
                         inputs.size());
        }
    }
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
        invalidFrames_.pop_front();
        ++dropped.invalid;
    }

    while (validFrames_.size() + invalidFrames_.size() > maxBufferedFrames && !validFrames_.empty()) {
        validFrames_.pop_front();
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
                invalidFrames_.pop_front();
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
    // Move the accumulated frames out (brief lock) so capture/processing never
    // blocks on the HDF5 write, then hand them to the write queue. The queue's
    // dedicated writer thread performs the slow append; capture keeps filling a
    // fresh buffer. Overflow or a write failure is fatal (stop + surface) rather
    // than a silent trim-and-drop.
    ExperimentBatch batch;
    {
        std::scoped_lock lk(framesMutex_);
        if (validFrames_.empty() && invalidFrames_.empty()) return 0;
        // Move-construct vectors from deques — cv::Mat moves are O(1) refcount transfers
        batch.valid.assign(std::make_move_iterator(validFrames_.begin()),
                           std::make_move_iterator(validFrames_.end()));
        batch.invalid.assign(std::make_move_iterator(invalidFrames_.begin()),
                             std::make_move_iterator(invalidFrames_.end()));
        validFrames_.clear();
        invalidFrames_.clear();
        framesSinceLastFlush_.store(0, std::memory_order_relaxed);
    }
    const size_t n = batch.valid.size() + batch.invalid.size();

    std::scoped_lock qlk(flushQueueMutex_);
    if (!flushQueue_) {
        Hdf5Service* h = &hdf5;
        auto writeFn = [this, h](const ExperimentBatch& b) -> bool {
            if (!h->appendFrames(b.valid, b.invalid)) return false;
            if (!b.valid.empty()) {
                totalValidFlushed_.fetch_add(static_cast<uint64_t>(b.valid.size()), std::memory_order_relaxed);
            }
            return true;
        };
        auto onError = [this](const std::string& msg) {
            if (flushErrorCb_) flushErrorCb_("Experiment save failed: " + msg);
        };
        flushQueue_ = std::make_unique<backend::recording::HdfWriteQueue<ExperimentBatch>>(3, writeFn, onError);
    }
    if (!flushQueue_->submit(std::move(batch))) {
        return 0; // fatal error already surfaced via onError
    }
    return n;
}

bool ProcessingService::finishFlush() {
    std::unique_ptr<backend::recording::HdfWriteQueue<ExperimentBatch>> q;
    {
        std::scoped_lock qlk(flushQueueMutex_);
        q = std::move(flushQueue_);
    }
    if (!q) return true;
    return q->flushAndStop(); // drains remaining batches, then joins
}

void ProcessingService::setFlushErrorCallback(std::function<void(const std::string&)> cb) {
    flushErrorCb_ = std::move(cb);
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

BrightnessQuantiles ProcessingService::calculateBrightnessQuantiles(const cv::Mat& originalImage, const cv::Mat& mask,
                                                                    const cv::Rect& region) {
    BrightnessQuantiles result;
    if (originalImage.empty() || mask.empty()) {
        return result;
    }

    // Avoid cloning when the input is already single-channel (the common case).
    cv::Mat converted;
    const cv::Mat* grayPtr = &originalImage;
    if (originalImage.channels() == 3) {
        cv::cvtColor(originalImage, converted, cv::COLOR_BGR2GRAY);
        grayPtr = &converted;
    }
    const cv::Mat& grayImage = *grayPtr;

    // Scan the whole image by default, or just the requested sub-rectangle,
    // clamped to the bounds shared by both images.
    cv::Rect scan(0, 0, std::min(grayImage.cols, mask.cols), std::min(grayImage.rows, mask.rows));
    if (region.width > 0 && region.height > 0) {
        scan &= region;
    }
    if (scan.width <= 0 || scan.height <= 0) {
        return result;
    }

    std::vector<uchar> brightness;
    brightness.reserve(static_cast<size_t>(scan.width) * static_cast<size_t>(scan.height) / 4 + 1);

    // Row-pointer access avoids the per-pixel bounds checks of cv::Mat::at<>.
    for (int y = scan.y; y < scan.y + scan.height; ++y) {
        const uchar* maskRow = mask.ptr<uchar>(y);
        const uchar* grayRow = grayImage.ptr<uchar>(y);
        for (int x = scan.x; x < scan.x + scan.width; ++x) {
            if (maskRow[x] > 0) {
                brightness.push_back(grayRow[x]);
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
                                                           const cv::Mat& originalImage,
                                                           const ContourAnalysis* measurement) {
    FilterResult result{};
    // allContours is assigned once (shared) by filterProcessedObjects after all
    // objects are evaluated; hierarchy is no longer retained on the result.
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
    const auto& geometryContour =
        (parentIdx >= 0 && parentIdx < static_cast<int>(analysis.filteredContours.size()))
            ? analysis.filteredContours[static_cast<size_t>(parentIdx)]
            : innerContour;
    populateGeometry(result, geometryContour);
    if (!originalImage.empty()) {
        const cv::Rect bbox(static_cast<int>(result.bboxX), static_cast<int>(result.bboxY),
                            static_cast<int>(result.bboxWidth), static_cast<int>(result.bboxHeight));
        result.brightness = calculateBrightnessQuantiles(originalImage, objectMask, bbox);
        computeFocusMetrics(originalImage, objectMask, bbox,
                            result.focusLaplacianVar, result.focusTenengrad);
    }

    if (config.enable_border_check && contourTouchesRoiBorder(innerContour, roi)) {
        result.touchesBorder = true;
        return result;
    }

    // Size basis: the adaptive detection contour by default, or — when a
    // fixed-threshold measurement mask is supplied — the measurement inner
    // contour that contains this object's hole, so area/deformability stay on a
    // contrast-independent basis. Fall back to the detection contour when the
    // measurement mask does not resolve a matching hole for this object.
    const std::vector<cv::Point>* sizeContour = &innerContour;
    if (measurement != nullptr) {
        const int mi = matchContourByOverlap(measurement->innerContours, innerContour,
                                             processedImage.size());
        if (mi >= 0) {
            sizeContour = &measurement->innerContours[static_cast<size_t>(mi)];
        }
    }

    const double contourArea = cv::contourArea(*sizeContour);
    if (contourArea <= 0.0) {
        return result;
    }

    std::vector<cv::Point> hull;
    cv::convexHull(*sizeContour, hull);
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
    const bool focusInRange = !config.enable_focus_check ||
        (result.focusLaplacianVar >= config.focus_laplacian_min);

    if (areaInRange && ringRatioInRange && deformabilityInRange && areaRatioInRange && focusInRange) {
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
                                                           const cv::Mat& originalImage,
                                                           const ContourAnalysis* measurement) {
    FilterResult result{};
    // allContours is assigned once (shared) by filterProcessedObjects after all
    // objects are evaluated; hierarchy is no longer retained on the result.
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
    populateGeometry(result, contour);
    if (!originalImage.empty()) {
        const cv::Rect bbox(static_cast<int>(result.bboxX), static_cast<int>(result.bboxY),
                            static_cast<int>(result.bboxWidth), static_cast<int>(result.bboxHeight));
        result.brightness = calculateBrightnessQuantiles(originalImage, objectMask, bbox);
        computeFocusMetrics(originalImage, objectMask, bbox,
                            result.focusLaplacianVar, result.focusTenengrad);
    }

    if (config.enable_border_check && contourTouchesRoiBorder(contour, roi)) {
        result.touchesBorder = true;
        return result;
    }

    // Size basis: the adaptive detection contour by default, or — when a
    // fixed-threshold measurement mask is supplied — the measurement blob that
    // contains this object, so area/deformability stay on a contrast-independent
    // basis. Fall back to the detection contour when no measurement blob matches.
    const std::vector<cv::Point>* sizeContour = &contour;
    if (measurement != nullptr) {
        const int mi = matchContourByOverlap(measurement->filteredContours, contour,
                                             processedImage.size());
        if (mi >= 0) {
            sizeContour = &measurement->filteredContours[static_cast<size_t>(mi)];
        }
    }

    const double contourArea = cv::contourArea(*sizeContour);
    if (contourArea <= 0.0) {
        return result;
    }

    std::vector<cv::Point> hull;
    cv::convexHull(*sizeContour, hull);
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
    const bool focusInRange = !config.enable_focus_check ||
        (result.focusLaplacianVar >= config.focus_laplacian_min);

    if (areaInRange && deformabilityInRange && areaRatioInRange && focusInRange) {
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
                                                                    const cv::Mat& originalImage,
                                                                    const cv::Mat& measurementMask) {
    const ContourAnalysis analysis = findContours(processedImage);

    // Fixed-threshold contours used only to re-measure object size (area,
    // areaRatio, deformability) when adaptive detection is on. Empty when
    // measurement is not decoupled, in which case size stays on `analysis`.
    ContourAnalysis measAnalysis;
    const ContourAnalysis* measurement = nullptr;
    if (!measurementMask.empty()) {
        measAnalysis = findContours(measurementMask);
        measurement = &measAnalysis;
    }

    // One shared copy of the frame's contours, referenced by every result (and
    // by the downstream monitoring / experiment copies) instead of deep-copying
    // all contour points per object.
    auto sharedContours =
        std::make_shared<const std::vector<std::vector<cv::Point>>>(analysis.allContours);

    FilterResult emptyResult{};
    emptyResult.allContours = sharedContours;
    emptyResult.innerContourCount = static_cast<int>(analysis.innerContours.size());
    emptyResult.hasSingleInnerContour = (analysis.innerContours.size() == 1);
    if (!originalImage.empty()) {
        emptyResult.brightness = calculateBrightnessQuantiles(originalImage, processedImage);
    }

    if (config.require_single_inner_contour && analysis.innerContours.empty()) {
        return {std::move(emptyResult)};
    }

    if (!analysis.innerContours.empty()) {
        std::vector<size_t> objectOrder;
        objectOrder.reserve(analysis.innerContours.size());
        for (size_t i = 0; i < analysis.innerContours.size(); ++i) {
            objectOrder.push_back(i);
        }
        std::sort(objectOrder.begin(), objectOrder.end(), [&](size_t lhs, size_t rhs) {
            const cv::Rect lhsBox = cv::boundingRect(analysis.innerContours[lhs]);
            const cv::Rect rhsBox = cv::boundingRect(analysis.innerContours[rhs]);
            return std::tie(lhsBox.x, lhsBox.y, lhs) < std::tie(rhsBox.x, rhsBox.y, rhs);
        });

        std::vector<FilterResult> results;
        results.reserve(objectOrder.size());
        const int objectCount = static_cast<int>(analysis.innerContours.size());
        for (size_t i = 0; i < objectOrder.size(); ++i) {
            results.push_back(evaluateInnerContourObject(analysis, objectOrder[i], static_cast<int>(i + 1), objectCount,
                                                         processedImage, roi, config, originalImage, measurement));
        }
        for (auto& result : results) {
            result.allContours = sharedContours;
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
        std::sort(topLevelContours.begin(), topLevelContours.end(), [&](size_t lhs, size_t rhs) {
            const cv::Rect lhsBox = cv::boundingRect(analysis.filteredContours[lhs]);
            const cv::Rect rhsBox = cv::boundingRect(analysis.filteredContours[rhs]);
            return std::tie(lhsBox.x, lhsBox.y, lhs) < std::tie(rhsBox.x, rhsBox.y, rhs);
        });

        std::vector<FilterResult> results;
        results.reserve(topLevelContours.size());
        const int objectCount = static_cast<int>(topLevelContours.size());
        for (size_t i = 0; i < topLevelContours.size(); ++i) {
            results.push_back(evaluateOuterContourObject(analysis, topLevelContours[i], static_cast<int>(i + 1),
                                                         objectCount, processedImage, roi, config, originalImage, measurement));
        }
        for (auto& result : results) {
            result.allContours = sharedContours;
        }
        return results;
    }

    return {std::move(emptyResult)};
}

FilterResult ProcessingService::filterProcessedImage(const cv::Mat& processedImage, const cv::Rect& roi,
                                                     const ProcessingConfig& config, const cv::Mat& originalImage,
                                                     const cv::Mat& measurementMask) {
    auto results = filterProcessedObjects(processedImage, roi, config, originalImage, measurementMask);
    if (results.empty()) {
        return {};
    }
    return std::move(results.front());
}

ProcessingService::BatchPipelineConfig ProcessingService::makeRealtimeBatchPipelineConfig() const {
    BatchPipelineConfig config;
    {
        std::scoped_lock settingsLk(rtBatchSettingsMutex_);
        config.batchSize = std::max<size_t>(1, rtBatchSettings_.batchSize);
        config.maxQueuedFrames = std::max(config.batchSize, rtBatchSettings_.maxQueuedFrames);
        config.workerCount = std::max<size_t>(1, rtBatchSettings_.workerCount);
        config.maxBatchDelayMs = std::max(1, rtBatchSettings_.maxBatchDelayMs);
    }
    {
        std::scoped_lock cfgLk(configMutex_);
        config.processing = processingConfig_;
    }
    {
        std::scoped_lock rtLk(rtMutex_);
        config.roi = rtRoi_;
        if (rtBgGray_ && !rtBgGray_->empty()) {
            config.background = rtBgGray_->clone();
        }
    }
    return config;
}

void ProcessingService::refreshRealtimeBatchPipelineConfig() {
    if (!rtBatchPipelineActive_.load(std::memory_order_acquire)) {
        return;
    }

    BatchPipelineConfig fresh = makeRealtimeBatchPipelineConfig();
    std::scoped_lock lk(batchMutex_);
    if (!rtBatchPipelineActive_.load(std::memory_order_relaxed)) {
        return;
    }
    batchConfig_.batchSize = fresh.batchSize;
    batchConfig_.maxQueuedFrames = fresh.maxQueuedFrames;
    batchConfig_.maxBatchDelayMs = fresh.maxBatchDelayMs;
    batchConfig_.processing = fresh.processing;
    batchConfig_.background = std::move(fresh.background);
    batchConfig_.roi = fresh.roi;
}

TargetGroupEvent ProcessingService::selectTargetGroupTriggerOwner(const std::vector<FilterResult>& validations) const {
    for (const auto& validation : validations) {
        if (!validation.isValid || !validation.isTargetGroup) {
            continue;
        }
        return {true, validation.objectId, validation.trackId};
    }
    return {};
}

void ProcessingService::publishRealtimeValidationCallbacks(const std::vector<FilterResult>& validations, uint64_t timestampNs) {
    const auto targetOwner = selectTargetGroupTriggerOwner(validations);
    if (targetOwner.isTargetGroup) {
        TargetGroupCallback tgCb;
        {
            std::scoped_lock cbLk(targetGroupCallbackMutex_);
            tgCb = targetGroupCallback_;
        }
        if (tgCb) tgCb(targetOwner);
    }

    // Hoist the callback copy out of the per-object loop: one mutex-guarded
    // std::function copy per frame, not per validation object (P7).
    RingRatioCallback rrCb;
    {
        std::scoped_lock cbLk(ringRatioCallbackMutex_);
        rrCb = ringRatioCallback_;
    }
    if (!rrCb) return;

    for (const auto& validation : validations) {
        if (!validation.isValid || validation.ringRatio <= 0.0) {
            continue;
        }
        rrCb(validation.ringRatio, static_cast<int64_t>(timestampNs));
    }
}

void ProcessingService::appendRealtimeMonitoringFrame(uint64_t index,
                                                      uint64_t timestampNs,
                                                      const FilterResult& validation,
                                                      const cv::Mat& originalImage,
                                                      const cv::Mat& processedImage) {
    if (!monitoringActive_.load(std::memory_order_relaxed)) return; // gating: no-op when inactive
    if (originalImage.empty() || processedImage.empty()) {
        return;
    }

    ProcessedFrame monitoringFrame;
    monitoringFrame.index = index;
    monitoringFrame.timestampNs = timestampNs;
    monitoringFrame.validation = validation;
    monitoringFrame.originalImage = originalImage; // shallow refcount share (frozen-mats invariant)
    monitoringFrame.processedImage = processedImage; // shallow refcount share

    std::scoped_lock monitoringLk(monitoringFramesMutex_);
    if (validation.isValid) {
        monitoringValidFrames_.push_back(std::move(monitoringFrame));
    } else {
        monitoringInvalidFrames_.push_back(std::move(monitoringFrame));
    }
}

void ProcessingService::publishRealtimeBatchFrame(ProcessedFrame&& frame) {
    if (frame.originalImage.empty() || frame.processedImage.empty()) {
        return;
    }

    const uint64_t frameIndex = frame.index;
    const FilterResult validation = frame.validation;

    // Monitoring (gated — skip when no consumer is active; ROI clones outside lock)
    if (monitoringActive_.load(std::memory_order_relaxed)) {
        Roi roi = getRealtimeRoi();
        if (roi.w <= 0 || roi.h <= 0) {
            roi.x = 0;
            roi.y = 0;
            roi.w = frame.originalImage.cols;
            roi.h = frame.originalImage.rows;
        }
        roi.x = std::max(0, std::min(roi.x, frame.originalImage.cols - 1));
        roi.y = std::max(0, std::min(roi.y, frame.originalImage.rows - 1));
        roi.w = std::max(1, std::min(roi.w, frame.originalImage.cols - roi.x));
        roi.h = std::max(1, std::min(roi.h, frame.originalImage.rows - roi.y));
        const cv::Rect cvRoi(roi.x, roi.y, roi.w, roi.h);

        ProcessedFrame monitoringFrame;
        monitoringFrame.index = frameIndex;
        monitoringFrame.timestampNs = frame.timestampNs;
        monitoringFrame.validation = validation;
        monitoringFrame.originalImage = frame.originalImage(cvRoi).clone();
        monitoringFrame.processedImage = frame.processedImage(cvRoi).clone();

        std::scoped_lock monitoringLk(monitoringFramesMutex_);
        if (validation.isValid) {
            monitoringValidFrames_.push_back(std::move(monitoringFrame));
        } else {
            monitoringInvalidFrames_.push_back(std::move(monitoringFrame));
        }
    }

    // Publish snapshot: build outside lock, pointer-swap inside (no full-frame copy under mutex)
    {
        auto newSnap = std::make_shared<RealtimeSnapshot>();
        newSnap->index = frameIndex;
        newSnap->mask = frame.processedImage; // shallow refcount share (frozen-mats invariant)
        newSnap->contours = validation.allContours
                                ? *validation.allContours
                                : std::vector<std::vector<cv::Point>>{};
        newSnap->validation = validation;
        std::scoped_lock snapshotLk(snapshotMutex_);
        latestSnapshot_ = std::move(newSnap); // O(1) pointer swap inside lock
    }

    uint64_t observed = rtLastProcessed_.load(std::memory_order_relaxed);
    while (frameIndex > observed &&
           !rtLastProcessed_.compare_exchange_weak(observed, frameIndex, std::memory_order_relaxed)) {
    }

    if (experimentActive_.load(std::memory_order_relaxed)) {
        bool shouldSave = validation.isValid;
        if (!validation.isValid) {
            const size_t counter = invalidFrameCounter_.fetch_add(1, std::memory_order_relaxed);
            const size_t rate = invalidFrameSamplingRate_.load(std::memory_order_relaxed);
            shouldSave = rate > 0 && (counter % rate) == 0;
        }
        if (shouldSave) {
            appendExperimentFrame(std::move(frame), validation.isValid);
        }
    }
}

void ProcessingService::realtimeBatchLoop() {
    rtLastProcessed_.store(0);

    std::atomic<uint64_t> callbackValid{0};
    std::atomic<uint64_t> callbackInvalid{0};

    const BatchPipelineConfig initialConfig = makeRealtimeBatchPipelineConfig();
    rtBatchPipelineActive_.store(true, std::memory_order_release);
    const bool started = startBatchPipeline(initialConfig,
                                            [this, &callbackValid, &callbackInvalid](std::vector<ProcessedFrame> batch) {
                                                std::vector<FilterResult> frameValidations;
                                                uint64_t lastFrameIndex = 0;
                                                uint64_t lastFrameTimestamp = 0;
                                                bool hasPendingFrame = false;

                                                for (auto& frame : batch) {
                                                    if (frame.validation.isValid) {
                                                        callbackValid.fetch_add(1, std::memory_order_relaxed);
                                                    } else {
                                                        callbackInvalid.fetch_add(1, std::memory_order_relaxed);
                                                    }
                                                    if (!hasPendingFrame) {
                                                        lastFrameIndex = frame.index;
                                                        lastFrameTimestamp = frame.timestampNs;
                                                        hasPendingFrame = true;
                                                    } else if (frame.index != lastFrameIndex ||
                                                               frame.timestampNs != lastFrameTimestamp) {
                                                        publishRealtimeValidationCallbacks(frameValidations, lastFrameTimestamp);
                                                        frameValidations.clear();
                                                        lastFrameIndex = frame.index;
                                                        lastFrameTimestamp = frame.timestampNs;
                                                    }

                                                    frameValidations.push_back(frame.validation);
                                                    publishRealtimeBatchFrame(std::move(frame));
                                                }

                                                if (hasPendingFrame && !frameValidations.empty()) {
                                                    publishRealtimeValidationCallbacks(frameValidations, lastFrameTimestamp);
                                                }
                                                });
    if (!started) {
        rtBatchPipelineActive_.store(false, std::memory_order_release);
        rtRunning_.store(false, std::memory_order_release);
        backend::diagnostics::CrashStateMirror::instance().processing.realtimeRunning.store(false);
        SPDLOG_ERROR("ProcessingService: async realtime batch mode could not start");
        return;
    }

    using clock = std::chrono::steady_clock;
    auto lastSummaryTs = clock::now();
    uint64_t queuedSinceSummary = 0;
    uint64_t skippedSinceSummary = 0;
    double enqueueMsSinceSummary = 0.0;
    uint64_t lastProcessedTotal = 0;
    uint64_t lastAlgoMicrosTotal = 0;

    SPDLOG_INFO("ProcessingService: realtime async batch loop started");
    if (initialConfig.processing.auto_background_enabled) {
        SPDLOG_WARN("ProcessingService: async batch realtime mode does not run inline auto-background capture; use frame-by-frame mode when auto-background capture is required");
    }
    if (initialConfig.processing.multi_image_enabled && initialConfig.processing.multi_image_count > 1) {
        SPDLOG_WARN("ProcessingService: async batch realtime mode records trigger frames only; use frame-by-frame mode for multi-image series capture");
    }

    while (rtRunning_.load(std::memory_order_acquire) &&
           getRealtimeProcessingMode() == RealtimeProcessingMode::AsyncBatch) {
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
        uint64_t last = rtLastProcessed_.load(std::memory_order_relaxed);
        if (last > latest) {
            // FrameStore::resize() renumbers frames from 0; a cached pointer
            // from the old numbering would idle this loop forever.
            SPDLOG_WARN("Async batch realtime pointer {} beyond latest {} (store resized?); resyncing", last, latest);
            last = latest;
            rtLastProcessed_.store(last, std::memory_order_relaxed);
        }
        if (last + 1 < earliest) {
            const uint64_t skipped = earliest - (last + 1);
            skippedSinceSummary += skipped;
            last = earliest - 1;
            rtLastProcessed_.store(last, std::memory_order_relaxed);
            SPDLOG_DEBUG("Async batch realtime fell behind, skipping {} frames (last={}, earliest={})",
                         skipped, last, earliest);
        }

        if (last >= latest) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } else {
            const bool dropFrames = rtDropFrames_.load(std::memory_order_relaxed) &&
                                    !experimentActive_.load(std::memory_order_relaxed);
            const uint64_t firstIdx = dropFrames ? latest : last + 1;
            if (dropFrames && last + 1 < firstIdx) {
                skippedSinceSummary += firstIdx - (last + 1);
            }

            for (uint64_t idx = firstIdx; idx <= latest && rtRunning_.load(std::memory_order_acquire); ++idx) {
                const auto enqueueStart = clock::now();
                if (!rtEnabled_.load(std::memory_order_relaxed)) {
                    rtLastProcessed_.store(idx, std::memory_order_relaxed);
                    continue;
                }

                backend::playback::Frame frame;
                if (!rtStore_->getByWriteIndex(idx, frame)) {
                    continue;
                }
                const bool accepted = enqueueBatchFrame(frame, idx);
                if (accepted) {
                    ++queuedSinceSummary;
                } else {
                    ++skippedSinceSummary;
                }
                rtLastProcessed_.store(idx, std::memory_order_relaxed);

                const auto enqueueEnd = clock::now();
                enqueueMsSinceSummary += std::chrono::duration<double, std::milli>(enqueueEnd - enqueueStart).count();
            }
        }

        const auto now = clock::now();
        const double windowMs = std::chrono::duration<double, std::milli>(now - lastSummaryTs).count();
        if (windowMs >= 1000.0) {
            const uint64_t processedTotal = batchFramesProcessed_.load(std::memory_order_relaxed);
            const uint64_t processedSinceSummary = processedTotal - lastProcessedTotal;
            lastProcessedTotal = processedTotal;

            const uint64_t algoMicrosTotal = batchAlgoMicrosTotal_.load(std::memory_order_relaxed);
            const uint64_t algoMicrosSinceSummary = algoMicrosTotal - lastAlgoMicrosTotal;
            lastAlgoMicrosTotal = algoMicrosTotal;

            const uint64_t validSinceSummary = callbackValid.exchange(0, std::memory_order_relaxed);
            const uint64_t invalidSinceSummary = callbackInvalid.exchange(0, std::memory_order_relaxed);
            const double fps = windowMs > 0.0 ? (static_cast<double>(processedSinceSummary) * 1000.0 / windowMs) : 0.0;
            const double vfps = windowMs > 0.0 ? (static_cast<double>(validSinceSummary) * 1000.0 / windowMs) : 0.0;
            const double ifps = windowMs > 0.0 ? (static_cast<double>(invalidSinceSummary) * 1000.0 / windowMs) : 0.0;
            const double algoAvgUs = processedSinceSummary > 0
                                         ? static_cast<double>(algoMicrosSinceSummary) / static_cast<double>(processedSinceSummary)
                                         : 0.0;
            algoFps1s_.store(fps, std::memory_order_relaxed);
            validFps1s_.store(vfps, std::memory_order_relaxed);
            invalidFps1s_.store(ifps, std::memory_order_relaxed);
            algoAvgUs1s_.store(algoAvgUs, std::memory_order_relaxed);
            algoAvgUs1sUpdatedUs_.store(backend::Tools::getTimestamp(), std::memory_order_relaxed);

            const auto stats = getBatchPipelineStats();
            SPDLOG_DEBUG("Realtime async batch summary: queued={} processed={} skipped={} dropped={} queue={} max_queue={} "
                         "window_ms={:.0f} enqueue_avg_ms={:.3f} algo_avg_us={:.1f} fps={:.1f}",
                         queuedSinceSummary, processedSinceSummary, skippedSinceSummary, stats.framesDropped,
                         stats.currentQueueDepth, stats.maxQueueDepth, windowMs,
                         queuedSinceSummary > 0 ? enqueueMsSinceSummary / static_cast<double>(queuedSinceSummary) : 0.0,
                         algoAvgUs, fps);

            lastSummaryTs = now;
            queuedSinceSummary = 0;
            skippedSinceSummary = 0;
            enqueueMsSinceSummary = 0.0;
        }
    }

    rtBatchPipelineActive_.store(false, std::memory_order_release);
    stopBatchPipeline();
    SPDLOG_INFO("ProcessingService: realtime async batch loop stopped");
}

void ProcessingService::realtimeLoop() {
    // An exception escaping a std::thread entry function is std::terminate —
    // the whole process dies on one bad frame. Catch, log, and restart the
    // loop instead (same policy as CaptureService::run).
    while (rtRunning_.load(std::memory_order_acquire)) {
        try {
            if (getRealtimeProcessingMode() == RealtimeProcessingMode::AsyncBatch) {
                realtimeBatchLoop();
            } else {
                realtimeInlineLoop();
            }
            break; // normal exit (stopRealtime or mode switch)
        } catch (const std::exception& ex) {
            SPDLOG_ERROR("ProcessingService: realtime loop exception: {} — restarting loop", ex.what());
        } catch (...) {
            SPDLOG_ERROR("ProcessingService: realtime loop unknown exception — restarting loop");
        }
        // The batch loop may have thrown before its own cleanup ran.
        rtBatchPipelineActive_.store(false, std::memory_order_release);
        stopBatchPipeline();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// FROZEN-MATS INVARIANT: every cv::Mat published from this loop (gray, mask,
// grayROI, grayFull, fullMask) is freshly allocated per iteration and never
// written after publication. All consumers (Hdf5Service, monitoring rings,
// overlay readers) are read-only. Shallow refcount assigns are therefore safe.
// If in-place buffer reuse is ever added here this invariant must be revisited.
void ProcessingService::realtimeInlineLoop() {
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

    // Hoisted config/roi/bg: refresh only when configVersion_ changes (P7)
    uint64_t lastRtConfigVer = std::numeric_limits<uint64_t>::max();
    Roi rtCachedRoi{};
    std::shared_ptr<cv::Mat> rtCachedBg;
    ProcessingConfig rtCachedConfig;

    while (rtRunning_.load()) {
        // Refresh config/roi/background only when something changed
        const uint64_t curRtConfigVer = configVersion_.load(std::memory_order_acquire);
        if (curRtConfigVer != lastRtConfigVer) {
            {
                std::scoped_lock lk(rtMutex_);
                rtCachedRoi = rtRoi_;
                rtCachedBg = rtBgGray_;
            }
            {
                std::scoped_lock lk(configMutex_);
                rtCachedConfig = processingConfig_;
            }
            lastRtConfigVer = curRtConfigVer;
        }
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
        if (last > latest) {
            // FrameStore::resize() renumbers frames from 0; a cached pointer
            // from the old numbering would idle this loop forever.
            SPDLOG_WARN("Realtime pointer {} beyond latest {} (store resized?); resyncing", last, latest);
            last = latest;
            rtLastProcessed_.store(last);
        }
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
            
            // Use hoisted config/roi/bg (refreshed at top of while loop when configVersion_ changed)
            Roi roi = rtCachedRoi;
            std::shared_ptr<cv::Mat> bgShared = rtCachedBg;
            ProcessingConfig config = rtCachedConfig;

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
                            cv::Mat fullGray = makeGrayCopy(f);
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
                applyProcessingThreshold(diffForProcessing, thresh, config);
                cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(morphK, morphK));
                cv::morphologyEx(thresh, mask, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), morphIter);
                cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), morphIter);

                // Fixed-threshold measurement mask (ROI-sized, matching `mask`);
                // empty unless adaptive detection is on.
                const cv::Mat measMask = buildMeasurementMask(diffForProcessing, mask.size(),
                                                              cv::Rect(0, 0, mask.cols, mask.rows),
                                                              morphK, morphIter, config);

                // Always run validation for monitoring (even without experiment)
                // mask is ROI-sized so contour coords are 0-based; use local roi for border check
                cv::Rect localRoi(0, 0, roi.w, roi.h);
                auto validations = filterProcessedObjects(mask, localRoi, config, grayROI, measMask);
                if (validations.empty()) {
                    validations.push_back(FilterResult{});
                }
                const FilterResult& validation = validations.front();

                // Extract contours from validation result and adjust coordinates for full-frame snapshot
                // Contours from filterProcessedImage are in ROI coordinates, need to adjust for full frame
                std::vector<std::vector<cv::Point>> contours =
                    validation.allContours ? *validation.allContours
                                           : std::vector<std::vector<cv::Point>>{};
                for (auto& contour : contours) {
                    for (auto& pt : contour) {
                        pt.x += roi.x;
                        pt.y += roi.y;
                    }
                }
                const auto algoEnd = clock::now();
                const double algoMs = std::chrono::duration<double, std::milli>(algoEnd - algoStart).count();
                algoMsSinceSummary += algoMs;
                for (const auto& objectValidation : validations) {
                    if (objectValidation.isValid) {
                        ++validSinceSummary;
                    } else {
                        ++invalidSinceSummary;
                    }
                }
                
                publishRealtimeValidationCallbacks(validations, f.timestamp);

                // Always accumulate frames for monitoring (with size limit)
                for (const auto& objectValidation : validations) {
                    appendRealtimeMonitoringFrame(idx, f.timestamp, objectValidation, grayROI, mask);
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
                const TargetGroupEvent targetOwner = selectTargetGroupTriggerOwner(validations);
                const FilterResult* triggerAnchor = nullptr;
                if (targetOwner.isTargetGroup) {
                    for (const auto& objectValidation : validations) {
                        if (objectValidation.isValid && objectValidation.isTargetGroup &&
                            objectValidation.objectId == targetOwner.objectId &&
                            objectValidation.trackId == targetOwner.trackId) {
                            triggerAnchor = &objectValidation;
                            break;
                        }
                    }
                }
                if (!triggerAnchor) {
                    const auto triggerFallbackIt = std::find_if(
                        validations.begin(), validations.end(),
                        [](const FilterResult& result) { return result.isValid; });
                    if (triggerFallbackIt != validations.end()) {
                        triggerAnchor = &(*triggerFallbackIt);
                    }
                }

                // Helper: lazy-init full-frame gray; returns a shallow refcount copy.
                // Frozen invariant: do not write through the returned Mat.
                auto makeFullGray = [&]() -> cv::Mat {
                    if (grayFull.empty()) {
                        grayFull = makeGrayCopy(f);
                    }
                    return grayFull; // shallow refcount copy — caller must not modify
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
                    if (triggerAnchor) {
                        if (multiImageMode) {
                            // Start new multi-image series
                            cv::Mat fullGray = makeFullGray();
                            cv::Mat fullMask(fullGray.rows, fullGray.cols, CV_8UC1, cv::Scalar(0));
                            cv::Rect fullCvRoi(roi.x, roi.y, roi.w, roi.h);
                            mask.copyTo(fullMask(fullCvRoi));

                            pendingMultiImageFrame = ProcessedFrame{};
                            pendingMultiImageFrame.index = idx;
                            pendingMultiImageFrame.timestampNs = f.timestamp;
                            pendingMultiImageFrame.validation = *triggerAnchor;
                            pendingMultiImageFrame.originalImage = fullGray; // shallow refcount share
                            pendingMultiImageFrame.processedImage = std::move(fullMask);
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
                        frame.validation = triggerAnchor ? *triggerAnchor : validation;
                        cv::Mat fullGray = makeFullGray();
                        cv::Mat fullMask(fullGray.rows, fullGray.cols, CV_8UC1, cv::Scalar(0));
                        cv::Rect fullCvRoi(roi.x, roi.y, roi.w, roi.h);
                        mask.copyTo(fullMask(fullCvRoi));
                        frame.originalImage = std::move(fullGray);
                        frame.processedImage = std::move(fullMask);

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

                // Publish snapshot: build outside lock, pointer-swap inside
                {
                    // Create full-size mask for snapshot display
                    cv::Mat fullMaskSnapshot;
                    if (useROI) {
                        // Reuse the frame we already fetched for this index. The
                        // experiment path may have built grayFull already; otherwise
                        // derive the snapshot from f directly instead of taking the
                        // contended FrameStore lock a second time for the same idx.
                        cv::Mat grayFullSnap;
                        if (!grayFull.empty()) {
                            grayFullSnap = grayFull;
                        } else if (!f.data.empty()) {
                            grayFullSnap = makeGrayCopy(f);
                        }
                        if (!grayFullSnap.empty()) {
                            fullMaskSnapshot = cv::Mat(grayFullSnap.rows, grayFullSnap.cols, CV_8UC1, cv::Scalar(0));
                            cv::Rect fullCvRoiSnap(roi.x, roi.y, roi.w, roi.h);
                            mask.copyTo(fullMaskSnapshot(fullCvRoiSnap));
                        } else {
                            fullMaskSnapshot = mask; // shallow (mask not modified after this)
                        }
                    } else {
                        fullMaskSnapshot = mask; // shallow (mask not modified after this)
                    }
                    auto newSnap = std::make_shared<RealtimeSnapshot>();
                    newSnap->index = idx;
                    newSnap->mask = std::move(fullMaskSnapshot);
                    newSnap->contours = std::move(contours);
                    newSnap->validation = validation;
                    std::scoped_lock lk(snapshotMutex_);
                    latestSnapshot_ = std::move(newSnap); // O(1) pointer swap inside lock
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
                cv::Mat gray = makeGrayCopy(f);
                if (gray.empty()) {
                    rtLastProcessed_.store(idx);
                    continue;
                }

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
                            cv::Mat fullGray = makeGrayCopy(f);
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
                applyProcessingThreshold(diffForProcessing, thresh, config);
                
                // Update previous frame for frame-to-frame comparison (when no background and auto-capture enabled)
                if (!hasBackground && config.auto_background_enabled && !experimentActive_.load()) {
                    std::scoped_lock prevFrameLk(previousFrameMutex_);
                    previousFrameForAutoCapture_ = blurredCurr; // share refcount; blurredCurr reallocs next iter
                }
                
                cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(morphK, morphK));
                cv::morphologyEx(thresh, roiDst, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), morphIter);
                cv::morphologyEx(roiDst, roiDst, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), morphIter);

                // Fixed-threshold measurement mask (full-frame, matching `mask`);
                // empty unless adaptive detection is on.
                const cv::Mat measMask = buildMeasurementMask(diffForProcessing, mask.size(), cvRoi,
                                                              morphK, morphIter, config);

                auto validations = filterProcessedObjects(mask, cvRoi, config, gray, measMask);
                if (validations.empty()) {
                    validations.push_back(FilterResult{});
                }
                const FilterResult& validation = validations.front();
                
                // Extract contours from validation result for snapshot
                std::vector<std::vector<cv::Point>> contours =
                    validation.allContours ? *validation.allContours
                                           : std::vector<std::vector<cv::Point>>{};
                const auto algoEnd = clock::now();
                const double algoMs = std::chrono::duration<double, std::milli>(algoEnd - algoStart).count();
                algoMsSinceSummary += algoMs;
                for (const auto& objectValidation : validations) {
                    if (objectValidation.isValid) {
                        ++validSinceSummary;
                    } else {
                        ++invalidSinceSummary;
                    }
                }
                
                publishRealtimeValidationCallbacks(validations, f.timestamp);

                // Always accumulate frames for monitoring (with size limit)
                cv::Mat roiOriginal = gray(cvRoi);
                cv::Mat roiMask = mask(cvRoi);
                for (const auto& objectValidation : validations) {
                    appendRealtimeMonitoringFrame(idx, f.timestamp, objectValidation, roiOriginal, roiMask);
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
                        frame.originalImage = gray; // shallow refcount share
                        frame.processedImage = mask; // shallow (mask used for snapshot below)

                        appendExperimentFrame(std::move(frame), validation.isValid);
                    }
                }

                // Publish snapshot: build outside lock, pointer-swap inside
                {
                    auto newSnap = std::make_shared<RealtimeSnapshot>();
                    newSnap->index = idx;
                    newSnap->mask = mask; // shallow refcount share (mask not modified after this)
                    newSnap->contours = std::move(contours);
                    newSnap->validation = validation;
                    std::scoped_lock lk(snapshotMutex_);
                    latestSnapshot_ = std::move(newSnap); // O(1) pointer swap inside lock
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
                const bool hasBg = (rtCachedBg != nullptr && !rtCachedBg->empty());
                const size_t flushInt = flushInterval_.load();
                const size_t sinceFlush = framesSinceLastFlush_.load();
                const double memMB = backend::Tools::getProcessMemoryMB();
                const double peakMB = backend::Tools::getPeakProcessMemoryMB();
                SPDLOG_DEBUG("Realtime buffers: acc_valid={} acc_invalid={} mon_valid={} mon_invalid={} flush_interval={} since_last_flush={} roi={}x{} bg={} mem_mb={:.1f} peak_mb={:.1f}",
                             vSz, iSz, monValidSz, monInvalidSz, flushInt, sinceFlush, rtCachedRoi.w, rtCachedRoi.h, hasBg ? 1 : 0, memMB, peakMB);
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
                cv::Mat gray = makeGrayCopy(f);
                if (gray.empty()) {
                    rtLastProcessed_.store(idx);
                    continue;
                }

                // Use hoisted config/roi/bg (refreshed at top of while loop when configVersion_ changed)
                Roi roi = rtCachedRoi;
                std::shared_ptr<cv::Mat> bgShared = rtCachedBg;
                ProcessingConfig config = rtCachedConfig;

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
                            cv::Mat fullGray = makeGrayCopy(f);
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
                        pendingMultiImageFrame.seriesImages.push_back(gray); // shallow refcount share (frozen-mats invariant)
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
                applyProcessingThreshold(diffForProcessing, thresh, config);

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
                // Fixed-threshold measurement mask, ROI-sized to match roiMaskForValidation;
                // empty unless adaptive detection is on.
                const cv::Mat measMask = buildMeasurementMask(diffForProcessing,
                                                              cv::Size(cvRoi.width, cvRoi.height),
                                                              cv::Rect(0, 0, cvRoi.width, cvRoi.height),
                                                              morphK, morphIter, config);
                auto validations = filterProcessedObjects(roiMaskForValidation, localRoi, config, roiCurr, measMask);
                if (validations.empty()) {
                    validations.push_back(FilterResult{});
                }
                const FilterResult& validation = validations.front();

                // Extract contours from validation result for snapshot
                // Contours are in ROI-relative coordinates — adjust to full-frame for snapshot/storage
                std::vector<std::vector<cv::Point>> contours =
                    validation.allContours ? *validation.allContours
                                           : std::vector<std::vector<cv::Point>>{};
                for (auto& contour : contours) {
                    for (auto& pt : contour) {
                        pt.x += roi.x;
                        pt.y += roi.y;
                    }
                }
                const auto algoEnd = clock::now();
                const double algoMs = std::chrono::duration<double, std::milli>(algoEnd - algoStart).count();
                algoMsSinceSummary += algoMs;
                for (const auto& objectValidation : validations) {
                    if (objectValidation.isValid) {
                        ++validSinceSummary;
                    } else {
                        ++invalidSinceSummary;
                    }
                }

                publishRealtimeValidationCallbacks(validations, f.timestamp);

                // Always accumulate frames for monitoring (with size limit)
                cv::Mat roiOriginal = gray(cvRoi);
                cv::Mat roiMask = mask(cvRoi);
                for (const auto& objectValidation : validations) {
                    appendRealtimeMonitoringFrame(idx, f.timestamp, objectValidation, roiOriginal, roiMask);
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
                    const TargetGroupEvent targetOwner = selectTargetGroupTriggerOwner(validations);
                    const FilterResult* triggerAnchor = nullptr;
                    if (targetOwner.isTargetGroup) {
                        for (const auto& objectValidation : validations) {
                            if (objectValidation.isValid && objectValidation.isTargetGroup &&
                                objectValidation.objectId == targetOwner.objectId &&
                                objectValidation.trackId == targetOwner.trackId) {
                                triggerAnchor = &objectValidation;
                                break;
                            }
                        }
                    }
                    if (!triggerAnchor) {
                        const auto triggerFallbackIt = std::find_if(
                            validations.begin(), validations.end(),
                            [](const FilterResult& result) { return result.isValid; });
                        if (triggerFallbackIt != validations.end()) {
                            triggerAnchor = &(*triggerFallbackIt);
                        }
                    }

                    if (multiImagePending) {
                        // We're collecting series images for a pending multi-image trigger frame
                        pendingMultiImageFrame.seriesImages.push_back(gray); // shallow refcount share
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
                        if (multiImageMode) {
                            if (triggerAnchor) {
                                const size_t validObjectCount = static_cast<size_t>(
                                    std::count_if(validations.begin(), validations.end(),
                                                  [](const FilterResult& result) { return result.isValid; }));
                                if (validObjectCount > 1) {
                                    SPDLOG_WARN("Multi-image realtime mode detected {} valid objects in frame {}; recording one trigger series",
                                                validObjectCount, idx);
                                }

                                pendingMultiImageFrame = ProcessedFrame{};
                                pendingMultiImageFrame.index = idx;
                                pendingMultiImageFrame.timestampNs = f.timestamp;
                                pendingMultiImageFrame.validation = *triggerAnchor;
                                pendingMultiImageFrame.originalImage = gray; // shallow refcount share
                                pendingMultiImageFrame.processedImage = mask; // shallow (mask used for snapshot below)
                                pendingMultiImageFrame.seriesImages.push_back(gray); // shallow refcount share
                                multiImageRemaining = static_cast<size_t>(config.multi_image_count - 1);
                                multiImagePending = true;
                                SPDLOG_DEBUG("Multi-image series started: trigger_idx={}, count={}", idx, config.multi_image_count);
                            }
                        } else {
                            for (const auto& objectValidation : validations) {
                                bool shouldSaveObject = objectValidation.isValid;
                                if (!objectValidation.isValid) {
                                    size_t counter = invalidFrameCounter_.fetch_add(1, std::memory_order_relaxed);
                                    size_t rate = invalidFrameSamplingRate_.load(std::memory_order_relaxed);
                                    shouldSaveObject = rate > 0 && (counter % rate) == 0;
                                }

                                if (shouldSaveObject) {
                                    ProcessedFrame frame;
                                    frame.index = idx;
                                    frame.timestampNs = f.timestamp;
                                    frame.validation = objectValidation;
                                    frame.originalImage = gray; // shallow (mask used for snapshot below)
                                    frame.processedImage = mask; // shallow (mask used for snapshot below)

                                    appendExperimentFrame(std::move(frame), objectValidation.isValid);
                                }
                            }
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

                // Publish snapshot: build outside lock, pointer-swap inside
                {
                    auto newSnap = std::make_shared<RealtimeSnapshot>();
                    newSnap->index = idx;
                    newSnap->mask = mask; // shallow refcount share (mask not modified after this)
                    newSnap->contours = std::move(contours);
                    newSnap->validation = validation;
                    std::scoped_lock lk(snapshotMutex_);
                    latestSnapshot_ = std::move(newSnap); // O(1) pointer swap inside lock
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
                    const bool hasBgDf = (rtCachedBg != nullptr && !rtCachedBg->empty());
                    const size_t flushInt = flushInterval_.load();
                    const size_t sinceFlush = framesSinceLastFlush_.load();
                    const double memMB = backend::Tools::getProcessMemoryMB();
                    const double peakMB = backend::Tools::getPeakProcessMemoryMB();
                    SPDLOG_DEBUG("Realtime buffers: acc_valid={} acc_invalid={} mon_valid={} mon_invalid={} flush_interval={} since_last_flush={} roi={}x{} bg={} mem_mb={:.1f} peak_mb={:.1f}",
                                 vSz, iSz, monValidSz, monInvalidSz, flushInt, sinceFlush, rtCachedRoi.w, rtCachedRoi.h, hasBgDf ? 1 : 0, memMB, peakMB);
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
