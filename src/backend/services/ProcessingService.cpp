#include "backend/services/ProcessingService.h"
#include "backend/services/Hdf5Service.h"
#include "backend/playback/FrameStore.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <tuple>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace backend::services {

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
    SPDLOG_INFO("ProcessingService stopped");
}

void ProcessingService::submit(Job job) {
    if (!running_.load()) return;
    {
        std::scoped_lock lk(mutex_);
        queue_.push(std::move(job));
        stats_.jobsQueued.fetch_add(1, std::memory_order_relaxed);
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
        }
    }
}

void ProcessingService::startRealtime(std::shared_ptr<backend::playback::FrameStore> store) {
    if (rtRunning_.load()) return;
    rtStore_ = std::move(store);
    rtRunning_.store(true);
    realtimeThread_ = std::thread(&ProcessingService::realtimeLoop, this);
    SPDLOG_INFO("ProcessingService: realtime processing started");
}

void ProcessingService::stopRealtime() {
    if (!rtRunning_.load()) return;
    rtRunning_.store(false);
    if (realtimeThread_.joinable()) realtimeThread_.join();
    SPDLOG_INFO("ProcessingService: realtime processing stopped");
}

void ProcessingService::setRealtimeEnabled(bool on) {
    rtEnabled_.store(on);
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
        rtBgGray_ = bg.clone();
    } else if (!bg.empty()) {
        cv::Mat tmp;
        bg.convertTo(tmp, CV_8UC1);
        rtBgGray_ = tmp.clone();
    } else {
        rtBgGray_.release();
    }
}

bool ProcessingService::getLatestSnapshot(RealtimeSnapshot& out) {
    std::scoped_lock lk(snapshotMutex_);
    if (latestSnapshot_.mask.empty() && latestSnapshot_.contours.empty()) return false;
    out.index = latestSnapshot_.index;
    out.mask = latestSnapshot_.mask.clone();
    out.contours = latestSnapshot_.contours;
    return true;
}

void ProcessingService::startExperiment() {
    std::scoped_lock lk(framesMutex_);
    validFrames_.clear();
    invalidFrames_.clear();
    // Reserve capacity to avoid frequent reallocations during accumulation
    // Estimate: at 5000 fps, 1000 frame flush interval = ~0.2 seconds = ~1000 frames
    validFrames_.reserve(flushInterval_.load());
    invalidFrames_.reserve(flushInterval_.load() * 10); // More invalid frames expected
    framesSinceLastFlush_.store(0);
    invalidFrameCounter_.store(0);
    experimentActive_.store(true);
    SPDLOG_INFO("ProcessingService: experiment started, frame buffers cleared (flush interval: {} frames, invalid sampling: every {}th)", 
                flushInterval_.load(), invalidFrameSamplingRate_.load());
}

void ProcessingService::endExperiment() {
    experimentActive_.store(false);
    SPDLOG_INFO("ProcessingService: experiment ended, valid frames: {}, invalid frames: {}", 
                validFrames_.size(), invalidFrames_.size());
}

std::vector<ProcessedFrame> ProcessingService::getValidFrames() const {
    std::scoped_lock lk(framesMutex_);
    return validFrames_;
}

std::vector<ProcessedFrame> ProcessingService::getInvalidFrames() const {
    std::scoped_lock lk(framesMutex_);
    return invalidFrames_;
}

void ProcessingService::clearAccumulatedFrames() {
    std::scoped_lock lk(framesMutex_);
    validFrames_.clear();
    invalidFrames_.clear();
}

void ProcessingService::setProcessingConfig(const ProcessingConfig& config) {
    std::scoped_lock lk(configMutex_);
    processingConfig_ = config;
}

ProcessingConfig ProcessingService::getProcessingConfig() const {
    std::scoped_lock lk(configMutex_);
    return processingConfig_;
}

size_t ProcessingService::flushBufferedFrames(class Hdf5Service& hdf5) {
    std::vector<ProcessedFrame> validToFlush;
    std::vector<ProcessedFrame> invalidToFlush;
    
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
        SPDLOG_INFO("HDF5 flush start: valid={}, invalid={}", validCount, invalidCount);
        const auto t0 = clock::now();
        const bool ok = hdf5.appendFrames(validToFlush, invalidToFlush);
        const auto t1 = clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (ok) {
            size_t flushed = validCount + invalidCount;
            framesSinceLastFlush_.store(0, std::memory_order_relaxed);
            SPDLOG_INFO("HDF5 flush end: flushed={} (valid={}, invalid={}) duration_ms={:.3f}",
                        flushed, validCount, invalidCount, ms);
            return flushed;
        } else {
            // Flush failed, put frames back
            std::scoped_lock lk(framesMutex_);
            validFrames_.insert(validFrames_.end(), validToFlush.begin(), validToFlush.end());
            invalidFrames_.insert(invalidFrames_.end(), invalidToFlush.begin(), invalidToFlush.end());
            SPDLOG_ERROR("HDF5 flush failed after {:.3f} ms; frames restored (valid={}, invalid={})",
                         ms, validCount, invalidCount);
            return 0;
        }
    }
    
    return 0;
}

void ProcessingService::setFlushInterval(size_t frames) {
    if (frames == 0) frames = 1; // Minimum 1
    flushInterval_.store(frames);
    SPDLOG_INFO("Flush interval set to: {} frames", frames);
}

size_t ProcessingService::getFlushInterval() const {
    return flushInterval_.load();
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
    if (outerArea <= 0) return 0.0;
    return std::sqrt(outerArea - innerArea);
}

std::tuple<std::vector<std::vector<cv::Point>>, bool, std::vector<std::vector<cv::Point>>, std::vector<int>> 
ProcessingService::findContours(const cv::Mat& processedImage) {
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(processedImage, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

    const double minNoiseArea = 10.0;
    std::vector<std::vector<cv::Point>> filteredContours;
    std::vector<cv::Vec4i> filteredHierarchy;

    for (size_t i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area >= minNoiseArea) {
            filteredContours.push_back(contours[i]);
            if (i < hierarchy.size()) {
                filteredHierarchy.push_back(hierarchy[i]);
            }
        }
    }

    bool hasNestedContours = false;
    std::vector<std::vector<cv::Point>> innerContours;
    std::vector<int> parentIndices;

    for (size_t i = 0; i < filteredHierarchy.size(); i++) {
        if (filteredHierarchy[i][3] > -1) {
            hasNestedContours = true;
            innerContours.push_back(filteredContours[i]);
            int parentIdx = filteredHierarchy[i][3];
            int filteredParentIdx = -1;
            for (size_t j = 0; j < filteredContours.size(); j++) {
                if (j == static_cast<size_t>(parentIdx)) {
                    filteredParentIdx = static_cast<int>(j);
                    break;
                }
            }
            parentIndices.push_back(filteredParentIdx);
        }
    }

    return std::make_tuple(filteredContours, hasNestedContours, innerContours, parentIndices);
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

FilterResult ProcessingService::filterProcessedImage(const cv::Mat& processedImage, const cv::Rect& roi, 
                                                     const ProcessingConfig& config, const cv::Mat& originalImage) {
    FilterResult result{};

    auto [contours, hasNestedContours, innerContours, parentIndices] = findContours(processedImage);
    
    result.innerContourCount = static_cast<int>(innerContours.size());
    result.hasSingleInnerContour = (innerContours.size() == 1);

    if (!originalImage.empty()) {
        result.brightness = calculateBrightnessQuantiles(originalImage, processedImage);
    }

    if (config.require_single_inner_contour && !result.hasSingleInnerContour) {
        return result;
    }

    // Border check
    if (config.enable_border_check) {
        const int borderThreshold = 2;
        if (!innerContours.empty()) {
            const auto& innerContour = innerContours[0];
            for (const auto& point : innerContour) {
                int x = point.x - roi.x;
                int y = point.y - roi.y;
                if (x >= 0 && x < roi.width && y >= 0 && y < roi.height) {
                    if (x < borderThreshold || x >= roi.width - borderThreshold ||
                        y < borderThreshold || y >= roi.height - borderThreshold) {
                        result.touchesBorder = true;
                        break;
                    }
                } else {
                    result.touchesBorder = true;
                    break;
                }
            }
        } else if (!contours.empty()) {
            for (const auto& contour : contours) {
                for (const auto& point : contour) {
                    int x = point.x - roi.x;
                    int y = point.y - roi.y;
                    if (x >= 0 && x < roi.width && y >= 0 && y < roi.height) {
                        if (x < borderThreshold || x >= roi.width - borderThreshold ||
                            y < borderThreshold || y >= roi.height - borderThreshold) {
                            result.touchesBorder = true;
                            break;
                        }
                    } else {
                        result.touchesBorder = true;
                        break;
                    }
                }
                if (result.touchesBorder) break;
            }
        }
    }

    if (!result.touchesBorder || !config.enable_border_check) {
        if (result.hasSingleInnerContour) {
            double contourArea = cv::contourArea(innerContours[0]);
            std::vector<cv::Point> hull;
            cv::convexHull(innerContours[0], hull);
            double hullArea = cv::contourArea(hull);
            result.areaRatio = hullArea / contourArea;
            double perimeter = cv::arcLength(hull, true);
            double circularity = (perimeter > 0) ? std::sqrt(4 * M_PI * hullArea) / perimeter : 0.0;
            result.deformability = 1.0 - circularity;
            result.area = hullArea;

            if (parentIndices.size() > 0) {
                int parentIdx = parentIndices[0];
                if (parentIdx >= 0 && parentIdx < static_cast<int>(contours.size())) {
                    result.ringRatio = calculateRingRatio(innerContours[0], contours[parentIdx]);
                }
            }

            bool areaInRange = !config.enable_area_range_check ||
                              (hullArea >= config.area_threshold_min && hullArea <= config.area_threshold_max);
            bool ringRatioInRange = (result.ringRatio > 15.0 && result.ringRatio < 25.0);

            if (areaInRange && ringRatioInRange) {
                result.inRange = true;
                result.isValid = true;
            }
        } else if (!contours.empty() && !config.require_single_inner_contour) {
            size_t largestIdx = 0;
            double largestOuterArea = 0.0;
            for (size_t i = 0; i < contours.size(); i++) {
                double area = cv::contourArea(contours[i]);
                if (area > largestOuterArea) {
                    largestOuterArea = area;
                    largestIdx = i;
                }
            }

            double contourArea = cv::contourArea(contours[largestIdx]);
            std::vector<cv::Point> hull;
            cv::convexHull(contours[largestIdx], hull);
            double hullArea = cv::contourArea(hull);
            result.areaRatio = hullArea / contourArea;
            double perimeter = cv::arcLength(hull, true);
            double circularity = (perimeter > 0) ? std::sqrt(4 * M_PI * hullArea) / perimeter : 0.0;
            result.deformability = 1.0 - circularity;
            result.area = hullArea;

            if (!config.enable_area_range_check ||
                (hullArea >= config.area_threshold_min && hullArea <= config.area_threshold_max)) {
                result.inRange = true;
                result.isValid = true;
            }
        }
    }

    return result;
}

static inline cv::Mat makeGrayCopy(uint64_t width, uint64_t height, size_t linePitch, const uint8_t* data) {
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    const size_t step = (linePitch == 0 ? static_cast<size_t>(width) : linePitch);
    cv::Mat view(h, w, CV_8UC1, const_cast<uint8_t*>(data), step);
    return view.clone();
}

void ProcessingService::realtimeLoop() {
    rtLastProcessed_.store(0);
    using clock = std::chrono::steady_clock;
    auto lastSummaryTs = clock::now();
    uint64_t framesSinceSummary = 0;
    uint64_t framesSkippedSinceSummary = 0;
    double msSinceSummary = 0.0;
    double algoMsSinceSummary = 0.0;

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

            // Grab ROI and background under lock
            Roi roi{};
            cv::Mat bg;
            {
                std::scoped_lock lk(rtMutex_);
                roi = rtRoi_;
                if (!rtBgGray_.empty()) bg = rtBgGray_;
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
            cv::Mat blurredCurr, blurredBg, diff, thresh;
            const auto algoStart = clock::now();
            cv::GaussianBlur(roiCurr, blurredCurr, cv::Size(3, 3), 0);
            if (!bg.empty() && bg.size() == gray.size() && bg.type() == CV_8UC1) {
                cv::GaussianBlur(bg(cvRoi), blurredBg, cv::Size(3, 3), 0);
                cv::subtract(blurredCurr, blurredBg, diff);
            } else {
                diff = blurredCurr;
            }
            cv::threshold(diff, thresh, 8, 255, cv::THRESH_BINARY);
            cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(3, 3));
            cv::morphologyEx(thresh, roiDst, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 1);
            cv::morphologyEx(roiDst, roiDst, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 1);

            // Contours
            std::vector<std::vector<cv::Point>> contours;
            std::vector<cv::Vec4i> hierarchy;
            cv::findContours(mask, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            // Validate and accumulate frames if experiment is active
            if (experimentActive_.load()) {
                ProcessingConfig config;
                {
                    std::scoped_lock cfgLk(configMutex_);
                    config = processingConfig_;
                }
                
                FilterResult validation = filterProcessedImage(mask, cvRoi, config, gray);
                const auto algoEnd = clock::now();
                const double algoMs = std::chrono::duration<double, std::milli>(algoEnd - algoStart).count();
                algoMsSinceSummary += algoMs;
                
                // Determine if we should save this frame
                // Always save valid frames, but sample invalid frames to reduce file size and improve performance
                bool shouldSave = false;
                if (validation.isValid) {
                    shouldSave = true; // Always save valid frames
                } else {
                    // Sample invalid frames: save every Nth invalid frame
                    size_t counter = invalidFrameCounter_.fetch_add(1, std::memory_order_relaxed);
                    size_t rate = invalidFrameSamplingRate_.load(std::memory_order_relaxed);
                    if (rate > 0 && (counter % rate) == 0) {
                        shouldSave = true;
                    }
                }
                
                if (shouldSave) {
                    // Prepare frame data (clone images before mutex lock to minimize lock time)
                    ProcessedFrame frame;
                    frame.index = idx;
                    frame.timestampNs = f.timestamp;
                    frame.validation = validation;
                    // Clone images - expensive but necessary to preserve data
                    frame.originalImage = gray.clone();
                    frame.processedImage = mask.clone();
                    
                    {
                        std::scoped_lock framesLk(framesMutex_);
                        // Use emplace_back with move to avoid extra copy
                        if (validation.isValid) {
                            validFrames_.emplace_back(std::move(frame));
                        } else {
                            invalidFrames_.emplace_back(std::move(frame));
                        }
                        
                        // Check if we should flush to disk (round-robin buffer)
                        size_t totalFrames = validFrames_.size() + invalidFrames_.size();
                        size_t interval = flushInterval_.load(std::memory_order_relaxed);
                        if (interval > 0 && totalFrames >= interval) {
                            // Signal that flush is needed (will be handled by MainWindow timer)
                            framesSinceLastFlush_.store(totalFrames, std::memory_order_relaxed);
                        }
                    }
                }
            } else {
                // Not accumulating frames; still record algorithm-only time
                const auto algoEnd = clock::now();
                const double algoMs = std::chrono::duration<double, std::milli>(algoEnd - algoStart).count();
                algoMsSinceSummary += algoMs;
            }

            // Publish snapshot
            {
                std::scoped_lock lk(snapshotMutex_);
                latestSnapshot_.index = idx;
                latestSnapshot_.mask = mask; // shallow copy ok; mask will be destroyed after leaving scope, so clone
                latestSnapshot_.mask = latestSnapshot_.mask.clone();
                latestSnapshot_.contours = std::move(contours);
            }

            rtLastProcessed_.store(idx);

            // Per-frame timing
            const auto frameEnd = clock::now();
            const double ms = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
            SPDLOG_DEBUG("Realtime processing: idx={} time_ms={:.3f} roi={}x{}", idx, ms, roi.w, roi.h);

            // Periodic summary
            framesSinceSummary += 1;
            msSinceSummary += ms;
            const auto now = frameEnd;
            const double windowMs = std::chrono::duration<double, std::milli>(now - lastSummaryTs).count();
            if (windowMs >= 1000.0) {
                const double avgMs = framesSinceSummary > 0 ? (msSinceSummary / static_cast<double>(framesSinceSummary)) : 0.0;
                const double algoAvgMs = framesSinceSummary > 0 ? (algoMsSinceSummary / static_cast<double>(framesSinceSummary)) : 0.0;
                const double fps = windowMs > 0.0 ? (static_cast<double>(framesSinceSummary) * 1000.0 / windowMs) : 0.0;
                SPDLOG_INFO("Realtime processing summary: processed={} skipped={} window_ms={:.0f} avg_ms={:.3f} algo_avg_ms={:.3f} ~fps={:.1f}",
                            framesSinceSummary, framesSkippedSinceSummary, windowMs, avgMs, algoAvgMs, fps);
                lastSummaryTs = now;
                framesSinceSummary = 0;
                framesSkippedSinceSummary = 0;
                msSinceSummary = 0.0;
                algoMsSinceSummary = 0.0;
            }
        }
    }
}

} // namespace backend::services
