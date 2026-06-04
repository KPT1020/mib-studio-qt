#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <condition_variable>
#include <opencv2/core.hpp>
#include <deque>
#include <cmath>
#include "backend/EModulusLut.h"

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
    int area_threshold_min{60};    // μm²
    int area_threshold_max{290};   // μm²
    double deformability_threshold_min{0.0};
    double deformability_threshold_max{1.0};
    bool enable_border_check{true};
    bool enable_area_range_check{true};
    bool enable_deformability_range_check{false};
    double area_ratio_threshold_max{1.5};
    bool enable_area_ratio_check{false};
    double ring_ratio_min{15.0};
    double ring_ratio_max{25.0};
    bool enable_ring_ratio_check{true};
    bool require_single_inner_contour{true};
    int empty_frame_pixel_threshold{100};
    bool auto_background_enabled{false};
    int auto_background_empty_frames{30};
    int auto_background_cooldown_frames{1000};
    // Target group sort trigger (second gate within valid frames)
    bool enable_target_group{false};
    int target_group_area_min{72};   // μm²
    int target_group_area_max{191};  // μm²
    double target_group_deformability_min{0.0};
    double target_group_deformability_max{0.3};
    // Young's modulus gating (uses LUT lookup from area + deformability)
    bool enable_target_group_emodulus{false};
    double target_group_emodulus_min{0.0};
    double target_group_emodulus_max{10.0};
    // Multi-image recording: capture a series of N consecutive frames per valid detection
    // Metrics are computed only from the first (trigger) frame
    bool multi_image_enabled{false};
    int multi_image_count{1}; // Number of images per series (1 = disabled, >1 = series)
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
    double youngsModulus{0.0}; // Young's modulus (kPa) from LUT lookup
    BrightnessQuantiles brightness;
    bool isTargetGroup{false}; // True if valid AND matches target group criteria
    // Contours found during processing (for snapshot/display)
    // These are in the same coordinate space as the processedImage mask
    std::vector<std::vector<cv::Point>> allContours;
    std::vector<cv::Vec4i> hierarchy;
};

struct DetectedObject {
    uint64_t objectId{0}; // Stable within a frame, ordered by contour discovery.
    uint64_t trackId{0};  // Assigned by offline batch tracking when enabled.
    cv::Rect boundingBox;
    cv::Point2d centroid{0.0, 0.0};
    bool isValid{false};
    bool touchesBorder{false};
    bool inRange{false};
    double deformability{0.0};
    double area{0.0};
    double areaRatio{0.0};
    double ringRatio{0.0};
    double youngsModulus{0.0};
    BrightnessQuantiles brightness;
};

struct ProcessedFrame {
    uint64_t index{0};
    uint64_t timestampNs{0};
    cv::Mat originalImage;
    cv::Mat processedImage; // mask
    FilterResult validation;
    int foregroundPixelCount{0};
    bool discardedEmpty{false};
    std::vector<DetectedObject> detections;
    // Multi-image series: additional images captured after the trigger frame.
    // seriesImages[0] is the trigger image (same as originalImage), followed by subsequent frames.
    // Empty when multi-image mode is disabled.
    std::vector<cv::Mat> seriesImages;
};

struct BufferedFrameCounts {
    size_t valid{0};
    size_t invalid{0};

    size_t total() const { return valid + invalid; }
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
        FilterResult validation;
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
    // When enabled, realtime processing will skip intermediate frames and process only the most recent frame.
    // Note: experiments still process every frame (this mode is ignored while experimentActive_ is true).
    void setRealtimeDropFrames(bool on);
    bool getRealtimeDropFrames() const { return rtDropFrames_.load(std::memory_order_relaxed); }
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
    BufferedFrameCounts getBufferedFrameCounts() const;
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
    size_t getMaxBufferedFrames() const;
    
    // Invalid frame sampling (save every Nth invalid frame to reduce file size)
    void setInvalidFrameSamplingRate(size_t rate); // Save every Nth invalid frame (default: 100, 1 = save all)
    size_t getInvalidFrameSamplingRate() const;

    // Configuration
    void setProcessingConfig(const ProcessingConfig& config);
    ProcessingConfig getProcessingConfig() const;
    
    // Pixel to micron conversion factor (1 pixel = X micron)
    void setPixelToMicronFactor(double factor);
    double getPixelToMicronFactor() const;
    
    // Realtime throughput metrics (1-second window)
    double getAlgoFps1s() const { return algoFps1s_.load(std::memory_order_relaxed); }
    double getValidFps1s() const { return validFps1s_.load(std::memory_order_relaxed); }
    double getInvalidFps1s() const { return invalidFps1s_.load(std::memory_order_relaxed); }
    void resetRealtimeMetrics() {
        algoFps1s_.store(0.0, std::memory_order_relaxed);
        validFps1s_.store(0.0, std::memory_order_relaxed);
        invalidFps1s_.store(0.0, std::memory_order_relaxed);
        algoAvgUs1s_.store(0.0, std::memory_order_relaxed);
        algoAvgUs1sUpdatedUs_.store(0, std::memory_order_relaxed);
    }
    
    // Totals for current experiment
    uint64_t getTotalValidFlushed() const { return totalValidFlushed_.load(std::memory_order_relaxed); }
    uint64_t getDroppedValidFrames() const { return droppedValidFrames_.load(std::memory_order_relaxed); }
    uint64_t getDroppedInvalidFrames() const { return droppedInvalidFrames_.load(std::memory_order_relaxed); }
    // Average algorithm processing time per frame over last 1s window (microseconds)
    double getAlgoAvgUs1s() const { return algoAvgUs1s_.load(std::memory_order_relaxed); }
    // Monotonic timestamp (microseconds) when algoAvgUs1s_ was last published; 0 if never
    uint64_t getAlgoAvgUs1sUpdatedUs() const { return algoAvgUs1sUpdatedUs_.load(std::memory_order_relaxed); }

    // Helper function to check if a raw frame is empty (for filtering during save)
    // Returns true if frame is empty (pixel count below threshold)
    static bool isFrameEmpty(const backend::playback::Frame& frame,
                            const ProcessingConfig& config,
                            const Roi& roi,
                            const cv::Mat& background = cv::Mat());

    // ---- Batch mask generation ----
    // Pure pipeline: Gaussian blur -> (optional) background subtract -> binary
    // threshold -> morphology -> contour validation. Produces a full-frame mask
    // (zero outside ROI) so outputs round-trip through Hdf5Service without
    // special handling. Does NOT touch realtime state, monitoring buffers,
    // experiment accumulation, or any callback.
    //
    // Inputs:
    //   grayInput          - CV_8UC1 image (or any type that can be converted)
    //   backgroundGray     - full-size CV_8UC1 background (empty = no subtract)
    //   config             - processing thresholds
    //   roi                - ROI to analyze; {0,0,0,0} means full frame
    //   index, timestampNs - copied into the ProcessedFrame
    //
    // Returns a ProcessedFrame with originalImage (full gray clone),
    // processedImage (full-size mask, CV_8UC1), and validation metrics filled.
    ProcessedFrame computeProcessedFrame(
        const cv::Mat& grayInput,
        const cv::Mat& backgroundGray,
        const ProcessingConfig& config,
        const Roi& roi,
        uint64_t index = 0,
        uint64_t timestampNs = 0);

    struct BatchProgress {
        size_t done{0};
        size_t total{0};
    };
    using BatchProgressCallback = std::function<void(const BatchProgress&)>;

    // Run computeProcessedFrame on each image in order. Does not modify
    // realtime config, monitoring buffers, experiment state, or fire any
    // callback. Safe to call from any thread.
    std::vector<ProcessedFrame> processBatch(
        const std::vector<cv::Mat>& grayImages,
        const ProcessingConfig& config,
        const cv::Mat& background = cv::Mat{},
        const Roi& roi = Roi{0, 0, 0, 0},
        BatchProgressCallback progress = {});

    struct BatchProcessingOptions {
        bool discardEmptyFrames{true};
        bool detectObjects{true};
        bool trackObjects{true};
        size_t workerCount{0}; // 0 = choose from hardware_concurrency.
        double maxTrackingDistancePx{32.0};
        size_t maxTrackGapFrames{2};
    };

    struct BatchTrack {
        uint64_t trackId{0};
        size_t firstFrameOffset{0};
        size_t lastFrameOffset{0};
        uint64_t firstFrameIndex{0};
        uint64_t lastFrameIndex{0};
        size_t observations{0};
    };

    struct BatchProcessingResult {
        size_t totalInputFrames{0};
        size_t discardedEmptyFrames{0};
        size_t processedFrameCount{0};
        size_t detectionCount{0};
        size_t uniqueObjectCount{0};
        std::vector<ProcessedFrame> frames;
        std::vector<BatchTrack> tracks;
    };

    // Offline recognition pipeline for recorded image sets. It computes masks,
    // discards empty frames before exposing detections, detects multiple object
    // candidates per frame, and optionally assigns track IDs for deduplication.
    BatchProcessingResult processBatchOffline(
        const std::vector<cv::Mat>& grayImages,
        const ProcessingConfig& config,
        const cv::Mat& background,
        const Roi& roi,
        const BatchProcessingOptions& options,
        BatchProgressCallback progress = {});

    // Async wrapper for processBatchOffline. Inputs are passed by value so the
    // background task does not depend on caller-owned cv::Mat lifetimes.
    std::future<BatchProcessingResult> processBatchAsync(
        std::vector<cv::Mat> grayImages,
        ProcessingConfig config,
        cv::Mat background,
        Roi roi,
        BatchProcessingOptions options,
        BatchProgressCallback progress = {});

    // Ring ratio callback for autofocus (called when validated frames are processed)
    using RingRatioCallback = std::function<void(double ringRatio, int64_t timestampNs)>;
    void setRingRatioCallback(RingRatioCallback callback);

    // Target group trigger callback (called for each valid frame with target group result)
    using TargetGroupCallback = std::function<void(bool isTargetGroup)>;
    void setTargetGroupCallback(TargetGroupCallback callback);

    // Young's modulus LUT loading
    bool loadEModulusLut(const std::string& path);

    // Background capture callback for auto-capture (called when background is auto-captured)
    using BackgroundCaptureCallback = std::function<void(const cv::Mat& background, uint64_t frameIndex)>;
    void setBackgroundCaptureCallback(BackgroundCaptureCallback callback);

private:
    struct DroppedFrameCounts {
        size_t valid{0};
        size_t invalid{0};
    };

    void workerLoop();
    void realtimeLoop();
    bool appendExperimentFrame(ProcessedFrame&& frame, bool isValid);
    DroppedFrameCounts trimExperimentBuffersLocked(size_t maxBufferedFrames);
    void logDroppedExperimentFrames(const DroppedFrameCounts& dropped, size_t bufferedTotal, size_t maxBufferedFrames);
    FilterResult filterProcessedImage(const cv::Mat& processedImage, const cv::Rect& roi, 
                                      const ProcessingConfig& config, const cv::Mat& originalImage);
    std::vector<DetectedObject> detectObjectsInProcessedImage(const cv::Mat& processedImage,
                                                              const cv::Rect& roi,
                                                              const ProcessingConfig& config,
                                                              const cv::Mat& originalImage);
    BrightnessQuantiles calculateBrightnessQuantiles(const cv::Mat& originalImage, const cv::Mat& mask);
    BrightnessQuantiles calculateBrightnessQuantiles(const cv::Mat& originalImage,
                                                     const std::vector<cv::Point>& contour);
    double calculateRingRatio(const std::vector<cv::Point>& innerContour, const std::vector<cv::Point>& outerContour);
    std::tuple<std::vector<std::vector<cv::Point>>, bool, std::vector<std::vector<cv::Point>>, std::vector<int>, 
               std::vector<std::vector<cv::Point>>, std::vector<cv::Vec4i>> 
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
    std::atomic<bool> rtDropFrames_{false};
    std::shared_ptr<backend::playback::FrameStore> rtStore_;
    mutable std::mutex rtMutex_;
    Roi rtRoi_{};
    std::shared_ptr<cv::Mat> rtBgGray_; // shared_ptr to avoid cloning on access
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

    // Simple circular buffer for ProcessedFrame (fixed capacity, keeps the most recent frames)
    class FrameRingBuffer {
    public:
        explicit FrameRingBuffer(size_t capacity = 1000)
            : capacity_(capacity), data_(capacity) {}

        void clear() {
            size_ = 0;
            head_ = 0;
        }

        size_t size() const { return size_; }
        size_t capacity() const { return capacity_; }

        void push_back(ProcessedFrame&& frame) {
            data_[head_] = std::move(frame);
            head_ = (head_ + 1) % capacity_;
            if (size_ < capacity_) {
                ++size_;
            }
        }

        // Copy out as oldest -> newest
        std::vector<ProcessedFrame> toVector() const {
            std::vector<ProcessedFrame> out;
            out.reserve(size_);
            const size_t start = (head_ + capacity_ - size_) % capacity_;
            for (size_t i = 0; i < size_; ++i) {
                out.push_back(data_[(start + i) % capacity_]);
            }
            return out;
        }

    private:
        size_t capacity_{0};
        std::vector<ProcessedFrame> data_;
        size_t size_{0};
        size_t head_{0}; // next write position
    };

    FrameRingBuffer monitoringValidFrames_{1000};
    FrameRingBuffer monitoringInvalidFrames_{1000};
    static constexpr size_t MAX_MONITORING_FRAMES = 1000; // Keep last 1000 frames for monitoring
    mutable ProcessingConfig processingConfig_;
    mutable std::mutex configMutex_;
    
    // Round-robin buffer for periodic flushing
    std::atomic<size_t> flushInterval_{100}; // Flush every 100 frames by default
    std::atomic<size_t> framesSinceLastFlush_{0};
    std::atomic<size_t> maxBufferedFrames_{1000};
    
    // Invalid frame sampling
    std::atomic<size_t> invalidFrameSamplingRate_{100}; // Save every 100th invalid frame by default
    std::atomic<size_t> invalidFrameCounter_{0}; // Counter for sampling
    
    // Ring ratio callback for autofocus
    mutable std::mutex ringRatioCallbackMutex_;
    RingRatioCallback ringRatioCallback_;

    mutable std::mutex targetGroupCallbackMutex_;
    TargetGroupCallback targetGroupCallback_;

    // Background capture callback for auto-capture
    mutable std::mutex backgroundCaptureCallbackMutex_;
    BackgroundCaptureCallback backgroundCaptureCallback_;
    
    // Auto-capture state tracking
    std::atomic<uint64_t> consecutiveEmptyFrames_{0};
    std::atomic<uint64_t> lastAutoBackgroundFrame_{0};
    cv::Mat previousFrameForAutoCapture_; // Store previous frame for frame-to-frame diff when no background
    std::mutex previousFrameMutex_; // Protect previous frame access
    
    // Realtime throughput metrics (published once per ~1s window)
    std::atomic<double> algoFps1s_{0.0};
    std::atomic<double> validFps1s_{0.0};
    std::atomic<double> invalidFps1s_{0.0};
    std::atomic<double> algoAvgUs1s_{0.0};
    std::atomic<uint64_t> algoAvgUs1sUpdatedUs_{0}; // monotonic us when algoAvgUs1s_ was last published
    
    // Experiment totals
    std::atomic<uint64_t> totalValidFlushed_{0};
    std::atomic<uint64_t> droppedValidFrames_{0};
    std::atomic<uint64_t> droppedInvalidFrames_{0};
    std::atomic<uint64_t> lastDropLogUs_{0};
    
    // Pixel to micron conversion factor (default: 0.4886)
    std::atomic<double> pixelToMicronFactor_{0.4886};

    // Young's modulus LUT (read-only after loading, thread-safe)
    EModulusLut eModulusLut_;
};

} // namespace backend::services
