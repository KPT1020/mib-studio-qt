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
#include "backend/processing/EModulusLut.h"
#include "backend/recording/HdfWriteQueue.h"

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
    // Per-frame adaptive (Otsu) segmentation threshold. When enabled, the
    // processing threshold is chosen by Otsu on the background-subtracted diff
    // instead of the fixed bg_subtract_threshold, then floored at
    // bg_subtract_threshold (so near-empty ROIs stay empty) and scaled by
    // otsu_scale. Off by default to preserve existing behaviour. See
    // benchmarks/mask-gen/REPORT.md for the accuracy/latency evidence.
    bool adaptive_threshold{false};
    double otsu_scale{1.1};
    // Prototype: the "proposed" segmentation front-end. Swaps the signed
    // cv::subtract diff for cv::absdiff (captures the full cell footprint, not
    // just the brighter-than-background part), forces the per-frame Otsu cut, and
    // closes/opens with an ellipse kernel. On the GT benchmark this lifts IoU
    // from ~0.34 to ~0.85 and area error vs truth from ~49% to ~10%
    // (benchmarks/mask-gen/area_accuracy.py). Off by default. The accuracy win is
    // the absdiff switch alone, so the top-hat is optional
    // (proposed_tophat_kernel, 0 = off). When on, size is measured on this
    // (accurate) mask directly, so the fixed-threshold decoupling is skipped.
    bool proposed_pipeline{false};
    int proposed_tophat_kernel{0};
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
    // Optional focus gate on focusLaplacianVar — a robust, always-defined
    // alternative to the fragile nested-contour ring ratio. Off by default, so
    // this is purely additive; enable once a per-setup threshold is chosen.
    bool enable_focus_check{false};
    double focus_laplacian_min{0.0};
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
    int objectId{-1};
    int objectCount{0};
    int trackId{-1};
    uint64_t trackFirstFrame{0};
    uint64_t trackLastFrame{0};
    int trackObservationCount{0};
    double bboxX{0.0};
    double bboxY{0.0};
    double bboxWidth{0.0};
    double bboxHeight{0.0};
    double centroidX{0.0};
    double centroidY{0.0};
    double deformability{0.0};
    double area{0.0};
    double areaRatio{0.0};
    double ringRatio{0.0};
    // Topology-free focus metrics computed from the original intensity inside the
    // object mask (do not depend on a closed nested ring contour). See
    // benchmarks/mask-gen/REPORT.md. Both are reported; gate on whichever suits.
    double focusLaplacianVar{0.0}; // variance of the Laplacian within the mask
    double focusTenengrad{0.0};    // mean Sobel gradient energy within the mask
    double youngsModulus{0.0}; // Young's modulus (kPa) from LUT lookup
    BrightnessQuantiles brightness;
    bool isTargetGroup{false}; // True if valid AND matches target group criteria
    // Contours found during processing (for snapshot/display), in the same
    // coordinate space as the processedImage mask. Shared (not deep-copied) so
    // that the per-object FilterResults of a frame, plus the monitoring /
    // experiment copies, all reference one allocation instead of duplicating
    // every contour point N times. Null when no contours were extracted.
    std::shared_ptr<const std::vector<std::vector<cv::Point>>> allContours;
};

struct TargetGroupEvent {
    bool isTargetGroup{false};
    int objectId{-1};
    int trackId{-1};
};

struct ProcessedFrame {
    uint64_t index{0};
    uint64_t timestampNs{0};
    cv::Mat originalImage;
    cv::Mat processedImage; // mask
    FilterResult validation;
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

// One unit of work handed to the experiment flush write queue.
struct ExperimentBatch {
    std::vector<ProcessedFrame> valid;
    std::vector<ProcessedFrame> invalid;
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

    enum class RealtimeProcessingMode {
        Inline = 0,
        AsyncBatch = 1,
    };

    struct RealtimeBatchSettings {
        size_t batchSize{16};
        size_t maxQueuedFrames{4096};
        size_t workerCount{1};
        int maxBatchDelayMs{10};
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
    bool isRealtimeRunning() const { return rtRunning_.load(std::memory_order_relaxed); }
    void setRealtimeEnabled(bool on);
    // When enabled, realtime processing will skip intermediate frames and process only the most recent frame.
    // Note: experiments still process every frame (this mode is ignored while experimentActive_ is true).
    void setRealtimeDropFrames(bool on);
    bool getRealtimeDropFrames() const { return rtDropFrames_.load(std::memory_order_relaxed); }
    void setRealtimeProcessingMode(RealtimeProcessingMode mode);
    RealtimeProcessingMode getRealtimeProcessingMode() const;
    void setRealtimeBatchSettings(const RealtimeBatchSettings& settings);
    RealtimeBatchSettings getRealtimeBatchSettings() const;
    void setRealtimeRoi(const Roi& roi);
    Roi getRealtimeRoi() const;
    void setRealtimeBackgroundGray(const cv::Mat& bg);
    cv::Mat getRealtimeBackgroundGray() const;
    // Zero-copy background accessor for hot paths; returns the shared_ptr directly.
    std::shared_ptr<const cv::Mat> getRealtimeBackgroundGrayShared() const;
    // Monotonic counter bumped by setProcessingConfig / setRealtimeRoi.
    uint64_t getConfigVersion() const;
    bool getLatestSnapshot(RealtimeSnapshot& out);

    // Experiment lifecycle
    void startExperiment();
    void endExperiment();
    
    // Frame accumulation access
    std::vector<ProcessedFrame> getValidFrames() const;
    std::vector<ProcessedFrame> getInvalidFrames() const;
    BufferedFrameCounts getBufferedFrameCounts() const;
    void clearAccumulatedFrames();
    
    // Monitoring frames (accumulated only while active; gate with setMonitoringActive)
    std::vector<ProcessedFrame> getMonitoringValidFrames() const;
    std::vector<ProcessedFrame> getMonitoringInvalidFrames() const;
    void clearMonitoringFrames();
    // Enable/disable monitoring accumulation. When false, appendRealtimeMonitoringFrame
    // returns immediately with no clones. Wire to tab show/hide in the UI.
    void setMonitoringActive(bool active);
    
    // Round-robin buffer flush (for crash resilience)
    // Returns number of frames flushed (submitted to the write queue)
    size_t flushBufferedFrames(class Hdf5Service& hdf5);

    // Drain and tear down the experiment write queue (call at experiment stop,
    // before writing experiment info). Returns false if a write error occurred.
    bool finishFlush();

    // Fatal flush-error sink: invoked (on the writer thread) when an experiment
    // flush write fails or the queue overflows. The experiment should stop.
    void setFlushErrorCallback(std::function<void(const std::string&)> cb);

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
    // Hot-path overload: extracts only the ROI (no full-frame copy) and reads
    // background via shared_ptr (no clone). Semantically identical to the above.
    static bool isFrameEmpty(const backend::playback::Frame& frame,
                            const ProcessingConfig& config,
                            const Roi& roi,
                            const std::shared_ptr<const cv::Mat>& background);

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
    // measurementMaskOut (optional): receives the fixed-threshold measurement
    // mask used to keep size metrics off the per-frame Otsu cut (empty when
    // adaptive_threshold is off). Callers that re-run filterProcessedObjects on
    // the returned processedImage (e.g. the batch paths, which need every object
    // for tracking) must forward it so their per-object size stays decoupled too.
    ProcessedFrame computeProcessedFrame(
        const cv::Mat& grayInput,
        const cv::Mat& backgroundGray,
        const ProcessingConfig& config,
        const Roi& roi,
        uint64_t index = 0,
        uint64_t timestampNs = 0,
        cv::Mat* measurementMaskOut = nullptr);

    struct BatchProgress {
        size_t done{0};
        size_t total{0};
    };
    using BatchProgressCallback = std::function<void(const BatchProgress&)>;

    // Process each image in order and emit one ProcessedFrame per detected
    // object candidate. Multiple records can share the same source index and
    // timestamp. Does not modify realtime config, monitoring buffers,
    // experiment state, or fire any callback. Safe to call from any thread.
    std::vector<ProcessedFrame> processBatch(
        const std::vector<cv::Mat>& grayImages,
        const ProcessingConfig& config,
        const cv::Mat& background = cv::Mat{},
        const Roi& roi = Roi{0, 0, 0, 0},
        BatchProgressCallback progress = {});

    struct BatchPipelineConfig {
        size_t batchSize{64};
        size_t maxQueuedFrames{4096};
        size_t workerCount{1};
        int maxBatchDelayMs{10};
        ProcessingConfig processing;
        cv::Mat background;
        Roi roi{0, 0, 0, 0};
    };

    struct BatchPipelineStats {
        uint64_t framesAccepted{0};
        uint64_t framesDropped{0};
        uint64_t framesProcessed{0};
        uint64_t batchesProcessed{0};
        size_t currentQueueDepth{0};
        size_t maxQueueDepth{0};
        size_t batchSize{0};
        size_t workerCount{0};
        bool running{false};
    };

    using BatchResultCallback = std::function<void(std::vector<ProcessedFrame>)>;

    // Async batch pipeline for capture-loop integration. enqueueBatchFrame()
    // copies the frame into a bounded queue and returns immediately. Dedicated
    // workers form configured-size batches, call computeProcessedFrame(), and
    // emit completed batches through the callback.
    bool startBatchPipeline(BatchPipelineConfig config, BatchResultCallback callback);
    void stopBatchPipeline();
    bool enqueueBatchFrame(const cv::Mat& grayImage, uint64_t index, uint64_t timestampNs = 0);
    bool enqueueBatchFrame(const backend::playback::Frame& frame, uint64_t index);
    BatchPipelineStats getBatchPipelineStats() const;

    // Ring ratio callback for autofocus (called when validated frames are processed)
    using RingRatioCallback = std::function<void(double ringRatio, int64_t timestampNs)>;
    void setRingRatioCallback(RingRatioCallback callback);

    // Target group trigger callback (one deterministic event per source frame)
    using TargetGroupCallback = std::function<void(const TargetGroupEvent& event)>;
    void setTargetGroupCallback(TargetGroupCallback callback);
    TargetGroupEvent selectTargetGroupTriggerOwner(const std::vector<FilterResult>& validations) const;

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

    struct QueuedBatchFrame {
        cv::Mat gray;
        uint64_t index{0};
        uint64_t timestampNs{0};
    };

    struct ContourAnalysis {
        std::vector<std::vector<cv::Point>> filteredContours;
        std::vector<std::vector<cv::Point>> innerContours;
        std::vector<int> parentIndices;
        std::vector<int> innerFilteredIndices;
        std::vector<std::vector<cv::Point>> allContours;
        std::vector<cv::Vec4i> hierarchy;
        std::vector<size_t> originalIndices;
    };

    void workerLoop();
    void batchWorkerLoop();
    void realtimeLoop();
    void realtimeInlineLoop();
    void realtimeBatchLoop();
    BatchPipelineConfig makeRealtimeBatchPipelineConfig() const;
    void refreshRealtimeBatchPipelineConfig();
    void publishRealtimeBatchFrame(ProcessedFrame&& frame);
    void publishRealtimeValidationCallbacks(const std::vector<FilterResult>& validations, uint64_t timestampNs);
    void appendRealtimeMonitoringFrame(uint64_t index,
                                       uint64_t timestampNs,
                                       const FilterResult& validation,
                                       const cv::Mat& originalImage,
                                       const cv::Mat& processedImage);
    bool appendExperimentFrame(ProcessedFrame&& frame, bool isValid);
    DroppedFrameCounts trimExperimentBuffersLocked(size_t maxBufferedFrames);
    void logDroppedExperimentFrames(const DroppedFrameCounts& dropped, size_t bufferedTotal, size_t maxBufferedFrames);
    // measurementMask (optional, same coordinate space as processedImage) carries
    // the fixed-threshold binary used to measure size when adaptive detection is
    // on. When non-empty, per-object area/areaRatio/deformability are re-derived
    // from it so those measurements do not drift with the per-frame Otsu cut. See
    // buildMeasurementMask and benchmarks/mask-gen/REPORT.md.
    FilterResult filterProcessedImage(const cv::Mat& processedImage, const cv::Rect& roi,
                                      const ProcessingConfig& config, const cv::Mat& originalImage,
                                      const cv::Mat& measurementMask = cv::Mat());
    std::vector<FilterResult> filterProcessedObjects(const cv::Mat& processedImage, const cv::Rect& roi,
                                                     const ProcessingConfig& config, const cv::Mat& originalImage,
                                                     const cv::Mat& measurementMask = cv::Mat());
    // region restricts the scan to a sub-rectangle (e.g. an object's bounding
    // box); an empty rect scans the whole image. Mask pixels outside an object's
    // bbox are zero, so restricting the scan yields an identical brightness set.
    BrightnessQuantiles calculateBrightnessQuantiles(const cv::Mat& originalImage, const cv::Mat& mask,
                                                     const cv::Rect& region = cv::Rect());
    double calculateRingRatio(const std::vector<cv::Point>& innerContour, const std::vector<cv::Point>& outerContour);
    cv::Mat makeObjectMask(const cv::Size& size,
                           const std::vector<std::vector<cv::Point>>& contours,
                           int contourIdx,
                           int parentIdx,
                           bool nested) const;
    bool contourTouchesRoiBorder(const std::vector<cv::Point>& contour, const cv::Rect& roi) const;
    // measurement (optional) is the ContourAnalysis of the fixed-threshold
    // measurement mask; when non-null, size metrics are read from the measurement
    // contour that corresponds to the detected object instead of the (adaptive)
    // detection contour.
    FilterResult evaluateInnerContourObject(const ContourAnalysis& analysis,
                                            size_t innerIdx,
                                            int objectId,
                                            int objectCount,
                                            const cv::Mat& processedImage,
                                            const cv::Rect& roi,
                                            const ProcessingConfig& config,
                                            const cv::Mat& originalImage,
                                            const ContourAnalysis* measurement = nullptr);
    FilterResult evaluateOuterContourObject(const ContourAnalysis& analysis,
                                            size_t contourIdx,
                                            int objectId,
                                            int objectCount,
                                            const cv::Mat& processedImage,
                                            const cv::Rect& roi,
                                            const ProcessingConfig& config,
                                            const cv::Mat& originalImage,
                                            const ContourAnalysis* measurement = nullptr);
    ContourAnalysis findContours(const cv::Mat& processedImage);
    // Index into `candidates` of the contour with the largest region overlap
    // with `object` (intersection over the smaller filled area), or -1 if the
    // best overlap is below minIoM. Maps a detected object to its counterpart on
    // the fixed-threshold measurement mask. Overlap (not centroid containment)
    // because the fixed mask can be fragmented/offset relative to the adaptive
    // detection — a strict point-in-polygon test misses those (validated on the
    // GT set in benchmarks/mask-gen/decoupled_bench.py: 62% -> 0% fallback).
    static int matchContourByOverlap(const std::vector<std::vector<cv::Point>>& candidates,
                                     const std::vector<cv::Point>& object,
                                     const cv::Size& size, double minIoM = 0.2);
    // Binarizes a background-subtracted diff into `thresh` using the configured
    // strategy: fixed bg_subtract_threshold, or per-frame Otsu floored at that
    // value and scaled by otsu_scale when config.adaptive_threshold is set.
    // Shared by computeProcessedFrame and the realtime loops.
    static void applyProcessingThreshold(const cv::Mat& diff, cv::Mat& thresh,
                                         const ProcessingConfig& config);
    // Fixed-threshold "measurement" mask: the binary the detection mask would be
    // with adaptive_threshold off (fixed bg_subtract_threshold + the same
    // close/open morphology), allocated at maskSize and morphed into morphRegion.
    // Returns an empty Mat when adaptive_threshold is off (callers then measure on
    // the detection mask). Lets size metrics stay on a stable, contrast-independent
    // basis while detection uses the per-frame Otsu cut.
    cv::Mat buildMeasurementMask(const cv::Mat& diff, const cv::Size& maskSize,
                                 const cv::Rect& morphRegion, int morphK, int morphIter,
                                 const ProcessingConfig& config) const;
    // Topology-free focus measures from the original intensity inside objectMask
    // (restricted to bbox for speed). High-pass, so the smooth background is
    // ignored — no background/diff input needed.
    static void computeFocusMetrics(const cv::Mat& originalImage, const cv::Mat& objectMask,
                                    const cv::Rect& bbox, double& lapVar, double& tenengrad);

    std::vector<std::thread> workers_;
    std::queue<Job> queue_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::atomic<bool> running_{false};

    ProcessingStats stats_{};

    // Async batch processing state
    std::vector<std::thread> batchWorkers_;
    std::queue<QueuedBatchFrame> batchQueue_;
    mutable std::mutex batchMutex_;
    std::condition_variable batchCv_;
    BatchPipelineConfig batchConfig_{};
    BatchResultCallback batchResultCallback_;
    std::atomic<bool> batchRunning_{false};
    std::atomic<uint64_t> batchFramesAccepted_{0};
    std::atomic<uint64_t> batchFramesDropped_{0};
    std::atomic<uint64_t> batchFramesProcessed_{0};
    std::atomic<uint64_t> batchBatchesProcessed_{0};
    std::atomic<uint64_t> batchAlgoMicrosTotal_{0};
    std::atomic<size_t> batchMaxQueueDepth_{0};
    std::atomic<size_t> batchWorkerCount_{0};

    // Realtime processing state
    std::thread realtimeThread_;
    std::atomic<bool> rtRunning_{false};
    std::atomic<bool> rtEnabled_{true};
    // Default ON so live view processes only the newest frame and the processed
    // overlay (mask/contours/target-group) cannot accumulate a backlog behind
    // the capture write head when capture outpaces processing. Experiments are
    // unaffected: the realtime loop ignores this flag while experimentActive_ is
    // true (gated by `rtDropFrames_ && !experimentActive_`), so every frame is
    // still processed/recorded during an experiment. Users can still turn it off
    // via ProcessingSettingsDialog. See e2e_live_view_latency_test.
    std::atomic<bool> rtDropFrames_{true};
    std::atomic<int> rtProcessingMode_{static_cast<int>(RealtimeProcessingMode::Inline)};
    mutable std::mutex rtBatchSettingsMutex_;
    RealtimeBatchSettings rtBatchSettings_{};
    std::atomic<bool> rtBatchPipelineActive_{false};
    std::shared_ptr<backend::playback::FrameStore> rtStore_;
    mutable std::mutex rtMutex_;
    Roi rtRoi_{};
    std::shared_ptr<cv::Mat> rtBgGray_; // shared_ptr to avoid cloning on access
    std::atomic<uint64_t> rtLastProcessed_{0};

    std::mutex snapshotMutex_;
    std::shared_ptr<const RealtimeSnapshot> latestSnapshot_; // pointer-swap on publish (no mutex-held copy)

    // Frame accumulation for experiment — deque for O(1) pop_front under backpressure
    mutable std::mutex framesMutex_;
    std::deque<ProcessedFrame> validFrames_;
    std::deque<ProcessedFrame> invalidFrames_;

    // Experiment flush write queue (decouples HDF5 writes from frame
    // accumulation). Created lazily on the first flush, drained by finishFlush.
    std::mutex flushQueueMutex_;
    std::unique_ptr<backend::recording::HdfWriteQueue<ExperimentBatch>> flushQueue_;
    std::function<void(const std::string&)> flushErrorCb_;
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
    std::atomic<bool> monitoringActive_{false}; // gating: no clones when no consumer is active
    mutable ProcessingConfig processingConfig_;
    mutable std::mutex configMutex_;
    std::atomic<uint64_t> configVersion_{0}; // bumped by setProcessingConfig / setRealtimeRoi
    
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
