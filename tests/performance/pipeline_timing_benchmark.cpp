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
#include <iostream>
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

} // namespace

int main() {
    std::cout << "pipeline timing benchmark (hardware_concurrency="
              << std::thread::hardware_concurrency() << ")\n";

    bool ok = true;
    ok = benchFrameStore() && ok;
    ok = benchBrightness() && ok;

    if (!ok) {
        std::cerr << "\npipeline timing benchmark FAILED\n";
        return 1;
    }
    std::cout << "\npipeline timing benchmark OK\n";
    return 0;
}
