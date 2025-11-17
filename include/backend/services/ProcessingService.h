#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <condition_variable>
#include <opencv2/core.hpp>
#include <deque>
#include <cmath>

namespace backend { namespace playback { class FrameStore; struct Frame; } }

namespace backend::services {

struct ProcessingStats {
    std::atomic<uint64_t> jobsQueued{0};
    std::atomic<uint64_t> jobsProcessed{0};
};

struct BrightnessQuantiles {
    double q1{0.0}; // 25th percentile
    double q2{0.0}; // 50th percentile (median)
    double q3{0.0}; // 75th percentile
    double q4{0.0}; // 100th percentile (max)
};

struct ProcessingConfig {
    int gaussian_blur_size{3};
    int bg_subtract_threshold{8};
    int morph_kernel_size{3};
    int morph_iterations{1};
    int area_threshold_min{250};
    int area_threshold_max{1000};
    bool enable_border_check{true};
    bool enable_area_range_check{true};
    bool require_single_inner_contour{true};
    int empty_frame_pixel_threshold{100};
};

struct FilterResult {
    bool isValid{false};
    bool touchesBorder{false};
    bool hasSingleInnerContour{false};
    bool inRange{false};
    int innerContourCount{0};
    double deformability{0.0};
    double area{0.0};
    double areaRatio{0.0};
    double ringRatio{0.0};
    BrightnessQuantiles brightness;
};

struct ProcessedFrame {
    uint64_t index{0};
    uint64_t timestampNs{0};
    cv::Mat originalImage;
    cv::Mat processedImage; // mask
    FilterResult validation;
};

class ProcessingService {
public:
    using Job = std::function<void()>;

    struct Roi {
        int x{0}, y{0}, w{0}, h{0};
    };

    struct RealtimeSnapshot {
        uint64_t index{0};
        std::vector<std::vector<cv::Point>> contours;
        cv::Mat mask;
    };

    ProcessingService();
    ~ProcessingService();

    void start(size_t workerCount = std::thread::hardware_concurrency());
    void stop();

    void submit(Job job);

    const ProcessingStats& stats() const { return stats_; }

    // Realtime processing API
    void startRealtime(std::shared_ptr<backend::playback::FrameStore> store);
    void stopRealtime();
    void setRealtimeEnabled(bool on);
    void setRealtimeRoi(const Roi& roi);
    Roi getRealtimeRoi() const;
    void setRealtimeBackgroundGray(const cv::Mat& bg);
    cv::Mat getRealtimeBackgroundGray() const;
    bool getLatestSnapshot(RealtimeSnapshot& out);

    // Experiment lifecycle
    void startExperiment();
    void endExperiment();
    
    // Frame accumulation access
    std::vector<ProcessedFrame> getValidFrames() const;
    std::vector<ProcessedFrame> getInvalidFrames() const;
    void clearAccumulatedFrames();
    
    // Monitoring frames (always available, even without experiment)
    std::vector<ProcessedFrame> getMonitoringValidFrames() const;
    std::vector<ProcessedFrame> getMonitoringInvalidFrames() const;
    void clearMonitoringFrames();
    
    // Round-robin buffer flush (for crash resilience)
    // Returns number of frames flushed
    size_t flushBufferedFrames(class Hdf5Service& hdf5);
    
    // Configuration for round-robin buffer
    void setFlushInterval(size_t frames); // Flush every N frames (default: 1000)
    size_t getFlushInterval() const;
    
    // Invalid frame sampling (save every Nth invalid frame to reduce file size)
    void setInvalidFrameSamplingRate(size_t rate); // Save every Nth invalid frame (default: 100, 1 = save all)
    size_t getInvalidFrameSamplingRate() const;

    // Configuration
    void setProcessingConfig(const ProcessingConfig& config);
    ProcessingConfig getProcessingConfig() const;

    // Helper function to check if a raw frame is empty (for filtering during save)
    // Returns true if frame is empty (pixel count below threshold)
    static bool isFrameEmpty(const backend::playback::Frame& frame,
                            const ProcessingConfig& config,
                            const Roi& roi,
                            const cv::Mat& background = cv::Mat());

    // Ring ratio callback for autofocus (called when validated frames are processed)
    using RingRatioCallback = std::function<void(double ringRatio, int64_t timestampNs)>;
    void setRingRatioCallback(RingRatioCallback callback);

private:
    void workerLoop();
    void realtimeLoop();
    FilterResult filterProcessedImage(const cv::Mat& processedImage, const cv::Rect& roi, 
                                      const ProcessingConfig& config, const cv::Mat& originalImage);
    BrightnessQuantiles calculateBrightnessQuantiles(const cv::Mat& originalImage, const cv::Mat& mask);
    double calculateRingRatio(const std::vector<cv::Point>& innerContour, const std::vector<cv::Point>& outerContour);
    std::tuple<std::vector<std::vector<cv::Point>>, bool, std::vector<std::vector<cv::Point>>, std::vector<int>> 
        findContours(const cv::Mat& processedImage);

    std::vector<std::thread> workers_;
    std::queue<Job> queue_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::atomic<bool> running_{false};

    ProcessingStats stats_{};

    // Realtime processing state
    std::thread realtimeThread_;
    std::atomic<bool> rtRunning_{false};
    std::atomic<bool> rtEnabled_{true};
    std::shared_ptr<backend::playback::FrameStore> rtStore_;
    mutable std::mutex rtMutex_;
    Roi rtRoi_{};
    cv::Mat rtBgGray_; // protected by rtMutex_
    std::atomic<uint64_t> rtLastProcessed_{0};

    std::mutex snapshotMutex_;
    RealtimeSnapshot latestSnapshot_;

    // Frame accumulation for experiment
    mutable std::mutex framesMutex_;
    std::vector<ProcessedFrame> validFrames_;
    std::vector<ProcessedFrame> invalidFrames_;
    std::atomic<bool> experimentActive_{false};
    
    // Monitoring frames (always accumulated, separate from experiment)
    mutable std::mutex monitoringFramesMutex_;
    std::vector<ProcessedFrame> monitoringValidFrames_;
    std::vector<ProcessedFrame> monitoringInvalidFrames_;
    static constexpr size_t MAX_MONITORING_FRAMES = 1000; // Keep last 1000 frames for monitoring
    mutable ProcessingConfig processingConfig_;
    mutable std::mutex configMutex_;
    
    // Round-robin buffer for periodic flushing
    std::atomic<size_t> flushInterval_{1000}; // Flush every 1000 frames by default
    std::atomic<size_t> framesSinceLastFlush_{0};
    
    // Invalid frame sampling
    std::atomic<size_t> invalidFrameSamplingRate_{100}; // Save every 100th invalid frame by default
    std::atomic<size_t> invalidFrameCounter_{0}; // Counter for sampling
    
    // Ring ratio callback for autofocus
    mutable std::mutex ringRatioCallbackMutex_;
    RingRatioCallback ringRatioCallback_;
};

} // namespace backend::services
