#include "backend/services/ProcessingService.h"
#include "backend/playback/FrameStore.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

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
    double msSinceSummary = 0.0;

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
            last = earliest - 1;
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
                const double fps = windowMs > 0.0 ? (static_cast<double>(framesSinceSummary) * 1000.0 / windowMs) : 0.0;
                SPDLOG_INFO("Realtime processing summary: frames={} window_ms={:.0f} avg_ms={:.3f} ~fps={:.1f}",
                            framesSinceSummary, windowMs, avgMs, fps);
                lastSummaryTs = now;
                framesSinceSummary = 0;
                msSinceSummary = 0.0;
            }
        }
    }
}

} // namespace backend::services
