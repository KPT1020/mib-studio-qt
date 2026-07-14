#include "backend/processing/IProcessingKernel.h"
#include "backend/processing/ProcessingService.h"
#include "support/assert.h"
#include "support/watchdog.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

namespace {

class StressKernel final : public backend::processing::IProcessingKernel {
public:
    explicit StressKernel(std::string version) {
        identity_.version = std::move(version);
        identity_.contractVersion = 1;
        identity_.engineAbiVersion = 1;
        identity_.source = "activation-stress";
    }

    const backend::processing::ProcessingCoreIdentity& identity() const noexcept override {
        return identity_;
    }

    bool processMask(const cv::Mat& gray,
                     const cv::Mat&,
                     const backend::processing::KernelConfig&,
                     const backend::processing::KernelRoi&,
                     cv::Mat& outputMask,
                     std::string*) override {
        processCalls.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        outputMask = cv::Mat::zeros(gray.rows, gray.cols, CV_8UC1);
        return true;
    }

    bool isEmpty(const cv::Mat&,
                 const cv::Mat&,
                 const backend::processing::KernelConfig&,
                 const backend::processing::KernelRoi&,
                 bool& outputIsEmpty,
                 std::string*) override {
        outputIsEmpty = false;
        return true;
    }

    bool reset(std::string*) override {
        resetCalls.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    std::atomic<uint64_t> processCalls{0};
    std::atomic<uint64_t> resetCalls{0};

private:
    backend::processing::ProcessingCoreIdentity identity_;
};

} // namespace

int main() {
    mib::test::Watchdog watchdog(20);
    backend::services::ProcessingService service;
    auto coreA = std::make_shared<StressKernel>("1.0.0-stress");
    auto coreB = std::make_shared<StressKernel>("2.0.0-stress");
    std::string error;

    watchdog.mark("activate A");
    MIB_REQUIRE(service.activateProcessingKernel(coreA, &error), error);

    constexpr int kWorkers = 4;
    constexpr int kFramesPerWorker = 150;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::atomic<uint64_t> completed{0};
    cv::Mat input = cv::Mat::zeros(16, 16, CV_8UC1);
    backend::services::ProcessingConfig config;
    config.require_single_inner_contour = false;

    std::vector<std::thread> workers;
    workers.reserve(kWorkers);
    for (int worker = 0; worker < kWorkers; ++worker) {
        workers.emplace_back([&, worker] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int frame = 0; frame < kFramesPerWorker; ++frame) {
                const auto result = service.computeProcessedFrame(
                    input, {}, config, {0, 0, input.cols, input.rows},
                    static_cast<uint64_t>(worker * kFramesPerWorker + frame), 0);
                if (!result.processedImage.empty()) {
                    completed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    watchdog.mark("release workers");
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (ready.load(std::memory_order_acquire) != kWorkers &&
           std::chrono::steady_clock::now() < readyDeadline) {
        std::this_thread::yield();
    }
    MIB_REQUIRE(ready.load(std::memory_order_acquire) == kWorkers,
                "all activation-stress workers reached the start barrier");
    start.store(true, std::memory_order_release);

    watchdog.mark("contended activation attempts");
    uint64_t successfulSwaps = 0;
    uint64_t rejectedSwaps = 0;
    for (int attempt = 0; attempt < 300; ++attempt) {
        auto candidate = (attempt % 2 == 0) ? coreB : coreA;
        error.clear();
        if (service.activateProcessingKernel(candidate, &error)) {
            ++successfulSwaps;
        } else {
            ++rejectedSwaps;
            MIB_EXPECT(error == "an offline processing operation is active",
                       "contended activation fails only at the operation-lease guard");
        }
        std::this_thread::yield();
    }

    watchdog.mark("join workers");
    for (auto& worker : workers) {
        worker.join(); // watchdog turns a deadlock into _Exit(99)
    }
    MIB_EXPECT(completed.load() == static_cast<uint64_t>(kWorkers * kFramesPerWorker),
               "every offered frame completes under activation contention");
    MIB_EXPECT(coreA->processCalls.load() + coreB->processCalls.load() == completed.load(),
               "frame accounting is conserved across resident cores");
    MIB_EXPECT(rejectedSwaps > 0,
               "outstanding operation leases reject at least one activation attempt");

    watchdog.mark("deterministic A to B to A");
    MIB_REQUIRE(service.activateProcessingKernel(coreA, &error), error);
    MIB_REQUIRE(service.activateProcessingKernel(coreB, &error), error);
    MIB_REQUIRE(service.activateProcessingKernel(coreA, &error), error);
    MIB_EXPECT(service.activeProcessingCoreIdentity().version == "1.0.0-stress",
               "A to B to A succeeds after contention reaches quiescence");
    MIB_EXPECT(successfulSwaps + rejectedSwaps == 300,
               "every activation attempt is explicitly accounted for");

    return mib::test::exitCode();
}
