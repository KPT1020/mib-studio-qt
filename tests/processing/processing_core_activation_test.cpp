#include "backend/playback/FrameStore.h"
#include "backend/processing/IProcessingKernel.h"
#include "backend/processing/ProcessingService.h"
#include "support/assert.h"
#include "support/watchdog.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

namespace {

class SpyKernel final : public backend::processing::IProcessingKernel {
public:
    explicit SpyKernel(std::string version) {
        identity_.version = std::move(version);
        identity_.contractVersion = 1;
        identity_.engineAbiVersion = 1;
        identity_.source = "test";
    }

    const backend::processing::ProcessingCoreIdentity& identity() const noexcept override {
        return identity_;
    }

    bool processMask(const cv::Mat& gray,
                     const cv::Mat&,
                     const backend::processing::KernelConfig&,
                     const backend::processing::KernelRoi&,
                     cv::Mat& mask,
                     std::string*) override {
        calls.fetch_add(1, std::memory_order_relaxed);
        mask = cv::Mat(gray.rows, gray.cols, CV_8UC1, cv::Scalar(255));
        return true;
    }

    bool isEmpty(const cv::Mat&,
                 const cv::Mat&,
                 const backend::processing::KernelConfig&,
                 const backend::processing::KernelRoi&,
                 bool& empty,
                 std::string*) override {
        emptyCalls.fetch_add(1, std::memory_order_relaxed);
        empty = false;
        return true;
    }

    bool reset(std::string*) override {
        resets.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    std::atomic<int> calls{0};
    std::atomic<int> emptyCalls{0};
    std::atomic<int> resets{0};

private:
    backend::processing::ProcessingCoreIdentity identity_;
};

} // namespace

int main() {
    mib::test::Watchdog watchdog(20);
    backend::services::ProcessingService service;
    auto spy = std::make_shared<SpyKernel>("9.9.9-test");
    std::string error;

    watchdog.mark("activate idle core");
    MIB_REQUIRE(service.activateProcessingKernel(spy, &error), error);
    MIB_EXPECT(service.activeProcessingCoreIdentity().version == "9.9.9-test",
               "active identity switches atomically");
    MIB_EXPECT(spy->resets.load() == 1, "candidate reset before activation");
    service.markProcessingCoreSelectionUnavailable();
    MIB_EXPECT(!service.isProcessingCorePinSatisfied(),
               "failed persisted selection makes processing unavailable");
    MIB_REQUIRE(service.activateProcessingKernel(spy, &error), error);
    MIB_EXPECT(service.isProcessingCorePinSatisfied(),
               "verified activation clears unavailable selection state");

    auto persistenceFailure = std::make_shared<SpyKernel>("9.9.10-test");
    bool failedPreCommitCalled = false;
    watchdog.mark("settings failure preserves active core");
    error.clear();
    MIB_EXPECT(!service.activateProcessingKernel(persistenceFailure, &error,
                                                 [&](std::string& commitError) {
                                                     failedPreCommitCalled = true;
                                                     commitError =
                                                         "injected QSettings sync failure";
                                                     return false;
                                                 }),
               "activation is refused when selection persistence fails");
    MIB_EXPECT(failedPreCommitCalled, "persistence pre-commit is exercised at quiescence");
    MIB_EXPECT(error == "injected QSettings sync failure",
               "persistence failure diagnostic is preserved");
    MIB_EXPECT(service.activeProcessingCoreIdentity().version == "9.9.9-test",
               "persistence failure preserves the previous active core");
    MIB_EXPECT(service.isProcessingCorePinSatisfied(),
               "persistence failure does not mark the previous core unavailable");

    cv::Mat input = cv::Mat::zeros(24, 32, CV_8UC1);
    backend::services::ProcessingConfig config;
    config.require_single_inner_contour = false;
    watchdog.mark("compute through selected core");
    const auto processed = service.computeProcessedFrame(input, {}, config, {0, 0, 32, 24});
    MIB_EXPECT(spy->calls.load() == 1, "computeProcessedFrame calls selected kernel");
    MIB_EXPECT(!processed.processedImage.empty() &&
                   cv::countNonZero(processed.processedImage) == 32 * 24,
               "selected kernel mask is returned");

    backend::playback::Frame raw;
    raw.width = 32;
    raw.height = 24;
    raw.linePitch = 32;
    raw.data.assign(32 * 24, 0);
    watchdog.mark("empty check through selected core");
    MIB_EXPECT(!service.isFrameEmptyWithActiveKernel(raw, config, {0, 0, 32, 24}, {}),
               "selected kernel owns empty-frame result");
    MIB_EXPECT(spy->emptyCalls.load() == 1, "selected empty check invoked");

    auto alternate = std::make_shared<SpyKernel>("10.0.0-test");
    watchdog.mark("experiment activation guard");
    service.startExperiment();
    int guardedPreCommitCalls = 0;
    MIB_EXPECT(!service.activateProcessingKernel(alternate, &error,
                                                 [&](std::string&) {
                                                     ++guardedPreCommitCalls;
                                                     return true;
                                                 }),
               "activation rejected during experiment");
    MIB_EXPECT(guardedPreCommitCalls == 0,
               "persistence pre-commit runs only after operation guards pass");
    service.endExperiment();

    watchdog.mark("batch activation guard");
    backend::services::ProcessingService::BatchPipelineConfig batchConfig;
    batchConfig.batchSize = 1;
    batchConfig.workerCount = 1;
    MIB_REQUIRE(service.startBatchPipeline(batchConfig, {}), "start batch pipeline");
    MIB_EXPECT(!service.activateProcessingKernel(alternate, &error),
               "activation rejected while batch worker is live");
    service.stopBatchPipeline();

    watchdog.mark("synchronous batch activation guard");
    std::mutex operationMutex;
    std::condition_variable operationCondition;
    bool operationEntered = false;
    bool releaseOperation = false;
    backend::processing::ProcessingCoreIdentity operationIdentity;
    std::vector<backend::services::ProcessedFrame> batchResults;
    std::thread operation([&] {
        batchResults = service.processBatch(
            {input}, config, {}, {0, 0, 32, 24},
            [&](const backend::services::ProcessingService::BatchProgress& progress) {
                if (progress.done != 0) return;
                std::unique_lock lock(operationMutex);
                operationEntered = true;
                operationCondition.notify_all();
                operationCondition.wait(lock, [&] { return releaseOperation; });
            },
            &operationIdentity);
    });
    {
        std::unique_lock lock(operationMutex);
        MIB_REQUIRE(operationCondition.wait_for(lock, std::chrono::seconds(5),
                                                [&] { return operationEntered; }),
                    "synchronous batch entered before timeout");
    }
    MIB_EXPECT(!service.activateProcessingKernel(alternate, &error),
               "activation rejected while synchronous batch owns the selected core");
    {
        std::scoped_lock lock(operationMutex);
        releaseOperation = true;
    }
    operationCondition.notify_all();
    operation.join(); // guarded by the test watchdog
    MIB_EXPECT(operationIdentity.version == "9.9.9-test",
               "synchronous batch captures the exact core identity it used");
    MIB_EXPECT(batchResults.size() == 1, "synchronous batch completes after release");

    watchdog.mark("realtime activation guard");
    auto store = std::make_shared<backend::playback::FrameStore>(8);
    const int emptyCallsBeforeRealtime = spy->emptyCalls.load();
    service.startRealtime(store);
    const auto realtimeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    uint64_t realtimeIndex = 1;
    while (spy->emptyCalls.load() == emptyCallsBeforeRealtime &&
           std::chrono::steady_clock::now() < realtimeDeadline) {
        store->pushFrame(raw.data.data(), raw.data.size(), raw.width, raw.height,
                         raw.linePitch, 0x01080001u, realtimeIndex++);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    MIB_EXPECT(spy->emptyCalls.load() > emptyCallsBeforeRealtime,
               "realtime empty-frame decision calls the selected kernel");
    MIB_EXPECT(!service.activateProcessingKernel(alternate, &error),
               "activation rejected while realtime is live");
    service.stopRealtime();

    watchdog.mark("activate after quiescence");
    int successfulPreCommitCalls = 0;
    MIB_REQUIRE(service.activateProcessingKernel(alternate, &error,
                                                 [&](std::string&) {
                                                     ++successfulPreCommitCalls;
                                                     return true;
                                                 }),
                error);
    MIB_EXPECT(successfulPreCommitCalls == 1,
               "successful activation persists exactly once before swapping");
    MIB_EXPECT(service.activeProcessingCoreIdentity().version == "10.0.0-test",
               "activation succeeds after every operation stops");
    MIB_REQUIRE(service.activateBundledProcessingKernel(&error), error);
    MIB_EXPECT(service.activeProcessingCoreIdentity().source == "bundled",
               "bundled fallback is an explicit activation");

    return mib::test::exitCode();
}
