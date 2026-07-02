// Timing benchmark for the image-pipeline throttling fixes. Two parts:
//
//  (A) FrameStore concurrency: the shipped per-slot-locked FrameStore vs a
//      LegacyRing that mirrors the pre-fix design (one global mutex held across
//      the full-frame copy). Runs 1 producer + N consumers over realistic
//      frames and reports combined full-frame-copy throughput + speedup.
//
//  (B) Brightness quantiles: the shipped bbox-restricted / row-pointer scan vs
//      the old full-ROI / cv::Mat::at<> scan. Asserts the two produce IDENTICAL
//      quantiles on every case (locking in the "bit-identical" claim) and
//      reports the per-call speedup.
//
// Timing is reported for humans. Gating assertions are correctness (B must be
// identical) plus loose, non-flaky throughput bounds so a future regression
// that reintroduces global serialization is caught without flaking on CI.

#include "backend/playback/FrameStore.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using backend::playback::Frame;
using backend::playback::FrameStore;
using Clock = std::chrono::steady_clock;

namespace {

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// ---------------------------------------------------------------------------
// (A) FrameStore concurrency
// ---------------------------------------------------------------------------

// Mirrors the pre-fix FrameStore hot path: a single std::mutex guards the whole
// ring and is held while the full frame is copied in (pushFrame) or out
// (getByWriteIndex). Same method names as FrameStore so the runner is generic.
class LegacyRing {
public:
    explicit LegacyRing(size_t capacity) : capacity_(capacity), ring_(capacity) {}

    void pushFrame(const uint8_t* src, size_t size, uint64_t width, uint64_t height,
                   size_t linePitch, uint64_t pixelFormat, uint64_t timestamp) {
        const uint64_t w = totalWritten_.fetch_add(1) + 1;
        const size_t idx = static_cast<size_t>((w - 1) % capacity_);
        std::scoped_lock lk(mutex_);
        Frame& f = ring_[idx];
        f.width = width;
        f.height = height;
        f.pixelFormat = pixelFormat;
        f.linePitch = linePitch;
        f.timestamp = timestamp;
        f.data.resize(size);
        std::copy_n(src, size, f.data.begin());
    }

    bool getByWriteIndex(uint64_t writeIndex, Frame& out) const {
        const uint64_t w = totalWritten_.load();
        if (writeIndex >= w || capacity_ == 0) return false;
        const size_t idx = static_cast<size_t>(writeIndex % capacity_);
        std::scoped_lock lk(mutex_);
        out = ring_[idx];
        return !out.data.empty();
    }

    uint64_t totalWritten() const { return totalWritten_.load(); }
    uint64_t latestAvailableIndex() const {
        const uint64_t w = totalWritten_.load();
        return w == 0 ? 0 : w - 1;
    }

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<Frame> ring_;
    std::atomic<uint64_t> totalWritten_{0};
};

struct ContentionResult {
    double seconds{0.0};
    uint64_t producerFrames{0};
    uint64_t consumerReads{0};
    double copiesPerSec{0.0}; // producer pushes + consumer reads per second
};

template <class Ring>
ContentionResult runContention(int frameW, int frameH, uint64_t kFrames, int nConsumers,
                               size_t capacity) {
    Ring ring(capacity);

    std::vector<uint8_t> frame(static_cast<size_t>(frameW) * static_cast<size_t>(frameH), 7);

    std::atomic<bool> done{false};
    std::atomic<uint64_t> reads{0};

    auto consumer = [&] {
        Frame out;
        uint64_t local = 0;
        while (!done.load(std::memory_order_relaxed)) {
            if (ring.totalWritten() == 0) continue;
            if (ring.getByWriteIndex(ring.latestAvailableIndex(), out)) {
                ++local;
            }
        }
        reads.fetch_add(local, std::memory_order_relaxed);
    };

    std::vector<std::thread> consumers;
    consumers.reserve(static_cast<size_t>(nConsumers));

    const auto start = Clock::now();
    for (int i = 0; i < nConsumers; ++i) {
        consumers.emplace_back(consumer);
    }
    for (uint64_t i = 0; i < kFrames; ++i) {
        frame[0] = static_cast<uint8_t>(i); // defeat any copy elision of the payload
        ring.pushFrame(frame.data(), frame.size(), static_cast<uint64_t>(frameW),
                       static_cast<uint64_t>(frameH), 0, 0x01080001, i);
    }
    done.store(true, std::memory_order_relaxed);
    for (auto& t : consumers) t.join();

    ContentionResult result;
    result.seconds = secondsSince(start);
    result.producerFrames = kFrames;
    result.consumerReads = reads.load(std::memory_order_relaxed);
    const double totalCopies = static_cast<double>(kFrames + result.consumerReads);
    result.copiesPerSec = result.seconds > 0.0 ? totalCopies / result.seconds : 0.0;
    return result;
}

bool benchFrameStore() {
    constexpr int kW = 512;
    constexpr int kH = 512; // 256 KiB/frame — copy cost dominates the lock hold
    constexpr uint64_t kFrames = 20000;
    constexpr size_t kCapacity = 256;
    const int consumers = std::max(2, static_cast<int>(std::thread::hardware_concurrency()) - 1);

    // Warm up allocators / page-in, discard result.
    (void)runContention<FrameStore>(kW, kH, 2000, consumers, kCapacity);

    const ContentionResult legacy = runContention<LegacyRing>(kW, kH, kFrames, consumers, kCapacity);
    const ContentionResult current = runContention<FrameStore>(kW, kH, kFrames, consumers, kCapacity);

    const double speedup = legacy.copiesPerSec > 0.0 ? current.copiesPerSec / legacy.copiesPerSec : 0.0;

    std::cout << "\n[A] FrameStore contention (" << kW << "x" << kH << ", "
              << kFrames << " frames, " << consumers << " consumers)\n";
    std::cout << "    legacy (single global mutex): " << static_cast<uint64_t>(legacy.copiesPerSec)
              << " copies/s  (" << legacy.consumerReads << " reads in " << legacy.seconds << "s)\n";
    std::cout << "    new    (per-slot locks)     : " << static_cast<uint64_t>(current.copiesPerSec)
              << " copies/s  (" << current.consumerReads << " reads in " << current.seconds << "s)\n";
    std::cout << "    speedup (new / legacy)      : " << speedup << "x\n";

    // Loose, non-flaky gate: the per-slot design must not be materially slower
    // than the global-mutex design. On multicore it is typically > 1x.
    if (current.copiesPerSec < legacy.copiesPerSec * 0.5) {
        std::cerr << "FAIL: new FrameStore throughput regressed below 0.5x legacy\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// (B) Brightness quantiles: legacy full-ROI scan vs new bbox/row-pointer scan
// ---------------------------------------------------------------------------

struct Quant {
    bool empty{true};
    int q1{0}, q2{0}, q3{0}, q4{0};
    bool operator==(const Quant& o) const {
        return empty == o.empty && q1 == o.q1 && q2 == o.q2 && q3 == o.q3 && q4 == o.q4;
    }
};

// Pre-fix algorithm: clone gray input, scan the whole image with cv::Mat::at<>.
Quant legacyBrightness(const cv::Mat& originalImage, const cv::Mat& mask) {
    Quant result;
    cv::Mat grayImage;
    if (originalImage.channels() == 3) {
        cv::cvtColor(originalImage, grayImage, cv::COLOR_BGR2GRAY);
    } else {
        grayImage = originalImage.clone();
    }
    std::vector<uchar> brightness;
    brightness.reserve(static_cast<size_t>(grayImage.rows) * grayImage.cols / 4);
    for (int y = 0; y < grayImage.rows; y++) {
        for (int x = 0; x < grayImage.cols; x++) {
            if (mask.at<uchar>(y, x) > 0) {
                brightness.push_back(grayImage.at<uchar>(y, x));
            }
        }
    }
    if (brightness.empty()) return result;
    std::sort(brightness.begin(), brightness.end());
    const size_t n = brightness.size();
    result.empty = false;
    result.q1 = brightness[n / 4];
    result.q2 = brightness[n / 2];
    result.q3 = brightness[(3 * n) / 4];
    result.q4 = brightness[n - 1];
    return result;
}

// Shipped algorithm: no clone for gray input, scan only `region` via row pointers.
Quant newBrightness(const cv::Mat& originalImage, const cv::Mat& mask, const cv::Rect& region) {
    Quant result;
    if (originalImage.empty() || mask.empty()) return result;

    cv::Mat converted;
    const cv::Mat* grayPtr = &originalImage;
    if (originalImage.channels() == 3) {
        cv::cvtColor(originalImage, converted, cv::COLOR_BGR2GRAY);
        grayPtr = &converted;
    }
    const cv::Mat& grayImage = *grayPtr;

    cv::Rect scan(0, 0, std::min(grayImage.cols, mask.cols), std::min(grayImage.rows, mask.rows));
    if (region.width > 0 && region.height > 0) {
        scan &= region;
    }
    if (scan.width <= 0 || scan.height <= 0) return result;

    std::vector<uchar> brightness;
    brightness.reserve(static_cast<size_t>(scan.width) * static_cast<size_t>(scan.height) / 4 + 1);
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
    const size_t n = brightness.size();
    result.empty = false;
    result.q1 = brightness[n / 4];
    result.q2 = brightness[n / 2];
    result.q3 = brightness[(3 * n) / 4];
    result.q4 = brightness[n - 1];
    return result;
}

bool benchBrightness() {
    constexpr int kImg = 256;       // ROI size
    constexpr int kIterations = 4000;
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> centerDist(40, kImg - 40);
    std::uniform_int_distribution<int> radiusDist(18, 34); // small object vs full ROI

    // Pre-generate cases so RNG / allocation cost is outside the timed regions.
    struct Case { cv::Mat image; cv::Mat mask; cv::Rect bbox; };
    std::vector<Case> cases;
    cases.reserve(kIterations);
    for (int i = 0; i < kIterations; ++i) {
        Case c;
        c.image.create(kImg, kImg, CV_8UC1);
        cv::randu(c.image, 0, 256);
        c.mask = cv::Mat::zeros(kImg, kImg, CV_8UC1);
        const cv::Point center(centerDist(rng), centerDist(rng));
        const int r = radiusDist(rng);
        cv::circle(c.mask, center, r, cv::Scalar(255), cv::FILLED);
        // bbox of the object; mask is zero outside it, so the new scan covers
        // exactly the pixels the legacy full scan would sample as non-zero.
        c.bbox = cv::Rect(center.x - r, center.y - r, 2 * r + 1, 2 * r + 1) &
                 cv::Rect(0, 0, kImg, kImg);
        cases.push_back(std::move(c));
    }

    // Correctness + timing in two separate passes so neither pollutes the other.
    for (const auto& c : cases) {
        if (!(legacyBrightness(c.image, c.mask) == newBrightness(c.image, c.mask, c.bbox))) {
            std::cerr << "FAIL: new brightness quantiles differ from legacy\n";
            return false;
        }
    }

    volatile int sink = 0;
    const auto legacyStart = Clock::now();
    for (const auto& c : cases) {
        sink += legacyBrightness(c.image, c.mask).q2;
    }
    const double legacySec = secondsSince(legacyStart);

    const auto newStart = Clock::now();
    for (const auto& c : cases) {
        sink += newBrightness(c.image, c.mask, c.bbox).q2;
    }
    const double newSec = secondsSince(newStart);
    (void)sink;

    const double legacyUs = legacySec * 1e6 / kIterations;
    const double newUs = newSec * 1e6 / kIterations;
    const double speedup = newSec > 0.0 ? legacySec / newSec : 0.0;

    std::cout << "\n[B] Brightness quantiles (" << kImg << "x" << kImg
              << " ROI, small object, " << kIterations << " cases)\n";
    std::cout << "    legacy (full scan, .at<>, clone): " << legacyUs << " us/call\n";
    std::cout << "    new    (bbox scan, row ptrs)     : " << newUs << " us/call\n";
    std::cout << "    speedup (legacy / new)           : " << speedup << "x  [results identical]\n";

    if (newUs > legacyUs * 1.05) {
        std::cerr << "FAIL: new brightness scan slower than legacy for small objects\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// (C) Recording hot-path: per-frame config lock + background clone +
//     full-frame isFrameEmpty vs hoisted reads + ROI-only isFrameEmpty.
//     Mirrors AppBackend recording lambda (lines ~933-937) and the current
//     ProcessingService::isFrameEmpty (full-frame makeGrayCopy).
// ---------------------------------------------------------------------------

// Legacy: allocates a full-frame gray copy, then slices the ROI for blur.
// Mirrors ProcessingService::isFrameEmpty (the makeGrayCopy + roi extraction path).
static int legacyFrameEmptyCheck(const uint8_t* src, int frameW, int frameH,
                                  int roiX, int roiY, int roiW, int roiH,
                                  const cv::Mat& bg, int threshold) {
    cv::Mat gray(frameH, frameW, CV_8UC1, const_cast<uint8_t*>(src));
    cv::Mat grayCopy = gray.clone(); // full-frame allocation (mirrors makeGrayCopy)
    const cv::Rect roi(roiX, roiY, roiW, roiH);
    cv::Mat blurredCurr, blurredBg, diff, thresh;
    cv::GaussianBlur(grayCopy(roi), blurredCurr, cv::Size(3, 3), 0);
    cv::GaussianBlur(bg(roi), blurredBg, cv::Size(3, 3), 0);
    cv::subtract(blurredCurr, blurredBg, diff);
    cv::threshold(diff, thresh, threshold, 255, cv::THRESH_BINARY);
    return cv::countNonZero(thresh);
}

// New: extracts only the ROI bytes from the raw frame — no full-frame allocation.
// Mirrors the proposed isFrameEmpty ROI overload (PR2).
static int newFrameEmptyCheck(const uint8_t* src, size_t pitch,
                               int roiX, int roiY, int roiW, int roiH,
                               const cv::Mat& bg, int threshold) {
    const uint8_t* roiSrc = src + static_cast<size_t>(roiY) * pitch + roiX;
    cv::Mat grayROI(roiH, roiW, CV_8UC1, const_cast<uint8_t*>(roiSrc), pitch);
    cv::Mat roiCopy = grayROI.clone(); // ROI-only allocation (matches makeGrayROI)
    const cv::Rect roi(roiX, roiY, roiW, roiH);
    cv::Mat blurredCurr, blurredBg, diff, thresh;
    cv::GaussianBlur(roiCopy, blurredCurr, cv::Size(3, 3), 0);
    cv::GaussianBlur(bg(roi), blurredBg, cv::Size(3, 3), 0); // shared bg — no clone
    cv::subtract(blurredCurr, blurredBg, diff);
    cv::threshold(diff, thresh, threshold, 255, cv::THRESH_BINARY);
    return cv::countNonZero(thresh);
}

bool benchRecordingHotPath() {
    constexpr int kW = 1280, kH = 1024; // realistic EGrabber camera sub-frame (~1.3 MB)
    constexpr int kRW = 128, kRH = 128; // typical cell detection ROI (128 µm × 128 µm)
    constexpr int kRX = (kW - kRW) / 2, kRY = (kH - kRH) / 2;
    constexpr uint64_t kFrames = 500;
    constexpr int kThreshold = 8;

    // Shared state mimicking ProcessingService members (configMutex_ + rtMutex_)
    int sharedThreshold = kThreshold;
    std::mutex configMutex;
    auto bgMat = std::make_shared<cv::Mat>(kH, kW, CV_8UC1, cv::Scalar(50));
    std::mutex rtMutex;

    const size_t frameBytes = static_cast<size_t>(kW) * kH;
    std::vector<uint8_t> frameData(frameBytes, 51);
    frameData[static_cast<size_t>(kRY) * kW + kRX] = 200; // non-empty pixel

    // Warm up allocators
    { cv::Mat tmp = bgMat->clone(); (void)tmp; }

    volatile int sink = 0;

    // Legacy: per-frame config lock + bg clone + full-frame gray copy (current code)
    const auto legacyStart = Clock::now();
    for (uint64_t i = 0; i < kFrames; ++i) {
        int threshold;
        {
            std::scoped_lock lk(configMutex);
            threshold = sharedThreshold; // per-frame config copy
        }
        cv::Mat bg;
        {
            std::scoped_lock lk(rtMutex);
            bg = bgMat->clone(); // per-frame full-frame background clone
        }
        frameData[0] = static_cast<uint8_t>(i);
        sink += legacyFrameEmptyCheck(frameData.data(), kW, kH,
                                      kRX, kRY, kRW, kRH, bg, threshold);
    }
    const double legacySec = secondsSince(legacyStart);

    // New: hoist config + shared_ptr once per poll batch; ROI-only per frame
    int threshold;
    {
        std::scoped_lock lk(configMutex);
        threshold = sharedThreshold;
    }
    std::shared_ptr<cv::Mat> bgShared;
    {
        std::scoped_lock lk(rtMutex);
        bgShared = bgMat; // shared_ptr copy — zero allocation, no data copy
    }

    const auto newStart = Clock::now();
    for (uint64_t i = 0; i < kFrames; ++i) {
        frameData[0] = static_cast<uint8_t>(i);
        sink += newFrameEmptyCheck(frameData.data(), static_cast<size_t>(kW),
                                   kRX, kRY, kRW, kRH, *bgShared, threshold);
    }
    const double newSec = secondsSince(newStart);
    (void)sink;

    const double legacyUs = legacySec * 1e6 / kFrames;
    const double newUs = newSec * 1e6 / kFrames;
    const double speedup = newSec > 0.0 ? legacySec / newSec : 0.0;

    std::cout << "\n[C] Recording per-frame overhead (" << kW << "x" << kH
              << " frame, " << kRW << "x" << kRH << " ROI cell, " << kFrames << " frames)\n";
    std::cout << "    legacy (per-frame lock + bg clone + full-frame gray): "
              << legacyUs << " us/frame\n";
    std::cout << "    new    (hoisted + shared_ptr + ROI-only gray)       : "
              << newUs << " us/frame\n";
    std::cout << "    speedup (legacy / new)                              : " << speedup << "x\n";

    // New path eliminates two full-frame allocations (bg clone + gray copy) per frame.
    // At 1280x1024 the per-clone cost (~1.3 MB memcpy) is substantial vs the 128x128 blur;
    // expect 1.5x+ but gate conservatively at 1.3x to be non-flaky across CI machines.
    if (speedup < 1.3) {
        std::cerr << "FAIL: hoisted+ROI path not materially faster than per-frame clone path\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// (D) Experiment buffer trim: vector front-erase O(n) vs deque pop_front O(1)
//     at a 10k-frame backlog — the saturated steady-state that causes O(n²)
//     total work in trimExperimentBuffersLocked (ProcessingService.cpp ~990-1014).
// ---------------------------------------------------------------------------

bool benchExperimentTrim() {
    constexpr size_t kBacklog = 10'000;
    constexpr size_t kTrimOps = 1'000;

    // Minimal stand-in for ProcessedFrame: two fields, no cv::Mat, so benchmark
    // focuses on container-operation cost (element shifts) not destructor cost.
    struct TrimItem {
        uint64_t index{0};
        uint64_t timestampNs{0};
    };

    // Legacy: vector with erase(begin()) — O(n) shift on every trim
    std::vector<TrimItem> vecBuf;
    vecBuf.reserve(kBacklog + kTrimOps);
    for (size_t i = 0; i < kBacklog; ++i) vecBuf.push_back({i, i});

    const auto legacyStart = Clock::now();
    for (size_t i = 0; i < kTrimOps; ++i) {
        vecBuf.push_back({kBacklog + i, kBacklog + i});
        vecBuf.erase(vecBuf.begin()); // O(n) shift — the bottleneck
    }
    const double legacySec = secondsSince(legacyStart);

    // New: deque with pop_front() — O(1)
    std::deque<TrimItem> deqBuf;
    for (size_t i = 0; i < kBacklog; ++i) deqBuf.push_back({i, i});

    const auto newStart = Clock::now();
    for (size_t i = 0; i < kTrimOps; ++i) {
        deqBuf.push_back({kBacklog + i, kBacklog + i});
        deqBuf.pop_front(); // O(1)
    }
    const double newSec = secondsSince(newStart);

    const double speedup = newSec > 0.0 ? legacySec / newSec : 0.0;
    const double legacyUsPerOp = legacySec * 1e6 / kTrimOps;
    const double newUsPerOp = newSec * 1e6 / kTrimOps;

    std::cout << "\n[D] Experiment buffer trim (" << kBacklog << "-item backlog, "
              << kTrimOps << " trim+push ops)\n";
    std::cout << "    legacy (vector erase(begin())): "
              << static_cast<uint64_t>(legacyUsPerOp) << " us/op  ("
              << legacySec * 1e3 << " ms total)\n";
    std::cout << "    new    (deque pop_front)       : "
              << static_cast<uint64_t>(newUsPerOp) << " us/op  ("
              << newSec * 1e3 << " ms total)\n";
    std::cout << "    speedup (legacy / new)         : " << speedup << "x\n";

    // At a 10k backlog, each vector erase shifts 10k elements; deque is O(1).
    // Expect 100x+; gate at 10x to be non-flaky on slow single-core CI.
    if (speedup < 10.0) {
        std::cerr << "FAIL: deque pop_front not 10x faster than vector erase(begin()) "
                     "at " << kBacklog << "-item backlog\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// (E) Snapshot publish/read contention: mutex-held mask.clone() (legacy) vs
//     clone-outside-mutex + pointer swap (new), under concurrent readers.
//     Mirrors ProcessingService snapshot publication (P5).
// ---------------------------------------------------------------------------

struct SnapshotContentionResult {
    double seconds{0.0};
    uint64_t publisherOps{0};
    uint64_t readerOps{0};
    double readsPerSec{0.0};
};

// Legacy: publisher clones inside snapshotMutex_; readers also clone inside it.
// Serializes all N readers against every publish (mask copy holds the mutex).
static SnapshotContentionResult runLegacySnapshotContention(int maskW, int maskH,
                                                              uint64_t publishOps,
                                                              int nReaders) {
    cv::Mat source(maskH, maskW, CV_8UC1, cv::Scalar(1));
    cv::Mat latestMask;
    std::mutex snapshotMutex;
    std::atomic<bool> done{false};
    std::atomic<uint64_t> reads{0};

    auto reader = [&] {
        cv::Mat out;
        uint64_t local = 0;
        while (!done.load(std::memory_order_relaxed)) {
            {
                std::scoped_lock lk(snapshotMutex);
                if (!latestMask.empty()) {
                    out = latestMask.clone(); // deep clone inside mutex (legacy)
                    ++local;
                }
            }
        }
        reads.fetch_add(local, std::memory_order_relaxed);
    };

    std::vector<std::thread> rthreads;
    rthreads.reserve(static_cast<size_t>(nReaders));
    for (int i = 0; i < nReaders; ++i) rthreads.emplace_back(reader);

    const auto start = Clock::now();
    for (uint64_t i = 0; i < publishOps; ++i) {
        source.data[0] = static_cast<uint8_t>(i);
        std::scoped_lock lk(snapshotMutex);
        latestMask = source.clone(); // publish: clone inside mutex (legacy)
    }
    done.store(true, std::memory_order_relaxed);
    for (auto& t : rthreads) t.join();

    SnapshotContentionResult r;
    r.seconds = secondsSince(start);
    r.publisherOps = publishOps;
    r.readerOps = reads.load();
    r.readsPerSec = r.seconds > 0.0 ? static_cast<double>(r.readerOps) / r.seconds : 0.0;
    return r;
}

// New: publisher clones outside snapshotMutex_, then pointer-swaps inside it.
// Readers take the mutex only to copy a shared_ptr (nanoseconds), then use the
// immutable snapshot without holding any lock.
static SnapshotContentionResult runNewSnapshotContention(int maskW, int maskH,
                                                          uint64_t publishOps,
                                                          int nReaders) {
    cv::Mat source(maskH, maskW, CV_8UC1, cv::Scalar(1));
    std::shared_ptr<cv::Mat> latestSnap = std::make_shared<cv::Mat>();
    std::mutex snapshotMutex;
    std::atomic<bool> done{false};
    std::atomic<uint64_t> reads{0};

    auto reader = [&] {
        uint64_t local = 0;
        while (!done.load(std::memory_order_relaxed)) {
            std::shared_ptr<cv::Mat> snap;
            {
                std::scoped_lock lk(snapshotMutex);
                snap = latestSnap; // O(1) shared_ptr copy inside mutex (new)
            }
            if (snap && !snap->empty()) ++local;
        }
        reads.fetch_add(local, std::memory_order_relaxed);
    };

    std::vector<std::thread> rthreads;
    rthreads.reserve(static_cast<size_t>(nReaders));
    for (int i = 0; i < nReaders; ++i) rthreads.emplace_back(reader);

    const auto start = Clock::now();
    for (uint64_t i = 0; i < publishOps; ++i) {
        source.data[0] = static_cast<uint8_t>(i);
        auto newSnap = std::make_shared<cv::Mat>(source.clone()); // clone outside mutex
        {
            std::scoped_lock lk(snapshotMutex);
            latestSnap = std::move(newSnap); // O(1) pointer swap inside mutex (new)
        }
    }
    done.store(true, std::memory_order_relaxed);
    for (auto& t : rthreads) t.join();

    SnapshotContentionResult r;
    r.seconds = secondsSince(start);
    r.publisherOps = publishOps;
    r.readerOps = reads.load();
    r.readsPerSec = r.seconds > 0.0 ? static_cast<double>(r.readerOps) / r.seconds : 0.0;
    return r;
}

bool benchSnapshotContention() {
    constexpr int kMaskW = 512;
    constexpr int kMaskH = 512;
    constexpr uint64_t kPublishOps = 2000;
    const int nReaders = std::max(2, static_cast<int>(std::thread::hardware_concurrency()) - 1);

    // Warm up allocators / threads
    (void)runLegacySnapshotContention(kMaskW, kMaskH, 100, nReaders);

    const SnapshotContentionResult legacy =
        runLegacySnapshotContention(kMaskW, kMaskH, kPublishOps, nReaders);
    const SnapshotContentionResult current =
        runNewSnapshotContention(kMaskW, kMaskH, kPublishOps, nReaders);

    const double readerSpeedup = legacy.readsPerSec > 0.0
                                     ? current.readsPerSec / legacy.readsPerSec
                                     : 0.0;

    std::cout << "\n[E] Snapshot publish/read contention (" << kMaskW << "x" << kMaskH
              << " mask, " << kPublishOps << " publishes, " << nReaders << " readers)\n";
    std::cout << "    legacy (clone inside mutex): "
              << static_cast<uint64_t>(legacy.readsPerSec)
              << " reads/s  (" << legacy.readerOps << " reads in " << legacy.seconds << "s)\n";
    std::cout << "    new    (ptr swap; clone outside): "
              << static_cast<uint64_t>(current.readsPerSec)
              << " reads/s  (" << current.readerOps << " reads in " << current.seconds << "s)\n";
    std::cout << "    reader speedup (new / legacy): " << readerSpeedup << "x\n";

    // New path holds the mutex for nanoseconds (pointer swap) vs microseconds (clone).
    // Readers must not be materially slower than with the legacy path — on a heavily
    // loaded single-core CI box the reader threads may not schedule well, so we use
    // the same 0.5x non-regression gate as Part (A).
    if (current.readsPerSec < legacy.readsPerSec * 0.5) {
        std::cerr << "FAIL: new snapshot read throughput regressed below 0.5x legacy\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::cout << "pipeline timing benchmark (hardware_concurrency="
              << std::thread::hardware_concurrency() << ")\n";

    bool ok = true;
    ok = benchFrameStore() && ok;
    ok = benchBrightness() && ok;
    ok = benchRecordingHotPath() && ok;
    ok = benchExperimentTrim() && ok;
    ok = benchSnapshotContention() && ok;

    if (!ok) {
        std::cerr << "\npipeline timing benchmark FAILED\n";
        return 1;
    }
    std::cout << "\npipeline timing benchmark OK\n";
    return 0;
}
