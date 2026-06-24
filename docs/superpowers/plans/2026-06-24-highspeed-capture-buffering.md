# High-speed Capture Buffering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decouple HDF5 writing from high-speed capture via a bounded 3-slot write queue, pre-reserve the FIFO so the per-frame hot path is allocation-free, and stop+alert on save failure instead of losing data silently.

**Architecture:** A header-only `HdfWriteQueue<Batch>` runs a dedicated writer thread draining a bounded FIFO; both experiment flush and recording submit batches to it. `FrameStore::reserveFrameBytes` pre-sizes ring slots at capture start. A backend fatal-error sink bridges to a `MainWindow` dialog that stops the operation.

**Tech Stack:** C++17, Qt 6.7.3 (Widgets), CMake/CTest, std::thread/condition_variable, HDF5 via Hdf5Service.

## Global Constraints

- On save failure OR sustained overflow (all slots in flight): latch a fatal error, stop the experiment/recording, surface it to the UI. Never count unsaved frames as written; never trust a partial file.
- Bounded round-robin = **3 slots** (queue capacity), one dedicated writer thread.
- HDF5 file format and the silent startup auto-check are unchanged; experiment and recording remain mutually exclusive on the shared `Hdf5Service` file.
- Pure/utility code is unit-tested off-hardware (mock the write); backend tests are bare `main()` linked to `mib_backend` (see `tests/CMakeLists.txt`, `tests/support/assert.h`).
- Every code change ships matching `knowledge_map/` vault updates; run `python scripts/check_docs.py` before any markdown/vault commit.

---

### Task 1: `HdfWriteQueue<Batch>` (bounded write queue + writer thread)

**Files:**
- Create: `include/backend/recording/HdfWriteQueue.h`
- Create: `tests/backend/hdf_write_queue_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `template<class Batch> class backend::recording::HdfWriteQueue`
  - `using WriteFn = std::function<bool(const Batch&)>; using ErrorFn = std::function<void(const std::string&)>;`
  - `HdfWriteQueue(size_t slots, WriteFn writeFn, ErrorFn onError)` — starts the writer thread.
  - `bool submit(Batch&& b)` — non-blocking; `false` if already errored or queue is full (full latches a fatal "overflow" error + fires `onError`).
  - `bool hasError() const; std::string error() const;`
  - `bool flushAndStop()` — drain queued batches, join writer; returns `true` iff no error occurred.
  - destructor calls `flushAndStop()` if not already stopped.

- [ ] **Step 1: Write the failing test** (`tests/backend/hdf_write_queue_test.cpp`)

```cpp
// hdf_write_queue_test — bounded write queue: FIFO drain, overflow=fatal,
// write-failure=fatal+onError-once, flushAndStop drains, no work after error.
#include "backend/recording/HdfWriteQueue.h"
#include "support/assert.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using backend::recording::HdfWriteQueue;

int main() {
    // 1) FIFO drain: every submitted batch is written, in order.
    {
        std::mutex m; std::vector<int> written;
        HdfWriteQueue<int> q(3,
            [&](const int& v){ std::scoped_lock lk(m); written.push_back(v); return true; },
            [](const std::string&){});
        for (int i = 0; i < 50; ++i) {
            // Give the writer room so we don't hit the 3-slot cap during this test.
            while (!q.submit(int(i))) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        MIB_REQUIRE(q.flushAndStop(), "clean drain, no error");
        MIB_REQUIRE(written.size() == 50, "all 50 written");
        bool ordered = true; for (int i = 0; i < 50; ++i) if (written[i] != i) ordered = false;
        MIB_EXPECT(ordered, "written in FIFO order");
    }

    // 2) Write failure -> fatal, onError fires exactly once, hasError set.
    {
        std::atomic<int> errCalls{0};
        HdfWriteQueue<int> q(3,
            [](const int&){ return false; },          // always fails
            [&](const std::string&){ errCalls.fetch_add(1); });
        q.submit(1);
        // wait for the writer to process + latch
        for (int i = 0; i < 200 && !q.hasError(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        MIB_REQUIRE(q.hasError(), "write failure latches error");
        MIB_EXPECT(!q.submit(2), "submit rejected after error");
        q.flushAndStop();
        MIB_EXPECT(errCalls.load() == 1, "onError fired exactly once");
    }

    // 3) Overflow -> fatal. A blocking writeFn keeps slots occupied so the
    //    queue fills and a further submit is rejected with an error.
    {
        std::atomic<bool> release{false};
        std::atomic<int> errCalls{0};
        HdfWriteQueue<int> q(3,
            [&](const int&){ while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); return true; },
            [&](const std::string&){ errCalls.fetch_add(1); });
        // First submit gets picked up by the (now blocked) writer; fill the 3 queue slots, then overflow.
        bool sawReject = false;
        for (int i = 0; i < 100; ++i) { if (!q.submit(int(i))) { sawReject = true; break; } std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        MIB_REQUIRE(sawReject, "submit eventually rejected on overflow");
        MIB_EXPECT(q.hasError(), "overflow latches fatal error");
        MIB_EXPECT(errCalls.load() == 1, "overflow fired onError once");
        release.store(true);
        q.flushAndStop();
    }

    if (mib::test::exitCode() == 0) std::printf("HdfWriteQueue FIFO/overflow/failure verified\n");
    return mib::test::exitCode();
}
```

- [ ] **Step 2: Register the test** in `tests/CMakeLists.txt` (after the `update_catalog_test` block; backend tests link `mib_backend`, but this header is standalone so use a dedicated target linking `Qt6::Core` is unnecessary — link `Threads`):

```cmake
mib_add_backend_test_executable(hdf_write_queue_test backend/hdf_write_queue_test.cpp)
find_package(Threads REQUIRED)
target_link_libraries(hdf_write_queue_test PRIVATE Threads::Threads)
add_test(NAME backend.hdf_write_queue COMMAND $<TARGET_FILE:hdf_write_queue_test>)
set_tests_properties(backend.hdf_write_queue PROPERTIES LABELS "backend" TIMEOUT 60)
```

- [ ] **Step 3: Verify it fails** — `cmake --build build --config Debug --target hdf_write_queue_test` → FAIL (header not found).

- [ ] **Step 4: Write the header** (`include/backend/recording/HdfWriteQueue.h`)

```cpp
// Bounded single-writer queue that decouples slow HDF5 writes from a fast
// producer. A dedicated thread drains a FIFO of at most `slots` batches and
// writes each via an injected writeFn. A failed write or a submit when full is
// a FATAL, latched error: the writer stops, onError fires once, and further
// submits are rejected. No HDF5/Qt dependency so it is unit-testable.
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace backend::recording {

template <class Batch>
class HdfWriteQueue {
public:
    using WriteFn = std::function<bool(const Batch&)>;
    using ErrorFn = std::function<void(const std::string&)>;

    HdfWriteQueue(size_t slots, WriteFn writeFn, ErrorFn onError)
        : slots_(slots == 0 ? 1 : slots), writeFn_(std::move(writeFn)), onError_(std::move(onError)) {
        worker_ = std::thread([this] { run(); });
    }
    ~HdfWriteQueue() { flushAndStop(); }

    HdfWriteQueue(const HdfWriteQueue&) = delete;
    HdfWriteQueue& operator=(const HdfWriteQueue&) = delete;

    bool submit(Batch&& b) {
        std::string fireMsg;
        {
            std::unique_lock<std::mutex> lk(mu_);
            if (error_) return false;
            if (queue_.size() >= slots_) {
                fireMsg = latchErrorLocked("write queue overflow (disk too slow)");
            } else {
                queue_.push_back(std::move(b));
                cv_.notify_one();
                return true;
            }
        }
        if (!fireMsg.empty()) fireError(fireMsg);
        return false;
    }

    bool hasError() const { std::unique_lock<std::mutex> lk(mu_); return error_; }
    std::string error() const { std::unique_lock<std::mutex> lk(mu_); return errorMsg_; }

    bool flushAndStop() {
        { std::unique_lock<std::mutex> lk(mu_); if (stopRequested_) { /* already */ } stopRequested_ = true; cv_.notify_all(); }
        if (worker_.joinable()) worker_.join();
        std::unique_lock<std::mutex> lk(mu_);
        return !error_;
    }

private:
    void run() {
        for (;;) {
            Batch b;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return !queue_.empty() || stopRequested_ || error_; });
                if (error_) return;
                if (queue_.empty()) { if (stopRequested_) return; else continue; }
                b = std::move(queue_.front());
                queue_.pop_front();
            }
            bool ok = false;
            std::string fireMsg;
            try { ok = writeFn_(b); }
            catch (const std::exception& e) { ok = false; fireMsg = std::string("write threw: ") + e.what(); }
            catch (...) { ok = false; fireMsg = "write threw unknown exception"; }
            if (!ok) {
                std::string msg;
                { std::unique_lock<std::mutex> lk(mu_); msg = latchErrorLocked(fireMsg.empty() ? "HDF5 write failed" : fireMsg); }
                if (!msg.empty()) fireError(msg);
                return; // stop writer on fatal error
            }
        }
    }

    // Sets the latched error under lock; returns the message to fire onError
    // with (empty if onError already fired, so the caller skips it).
    std::string latchErrorLocked(const std::string& msg) {
        if (!error_) { error_ = true; errorMsg_ = msg; }
        if (!onErrorFired_) { onErrorFired_ = true; return errorMsg_; }
        return std::string();
    }
    void fireError(const std::string& msg) { if (onError_) onError_(msg); }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Batch> queue_;
    bool stopRequested_ = false;
    bool error_ = false;
    bool onErrorFired_ = false;
    std::string errorMsg_;
    const size_t slots_;
    WriteFn writeFn_;
    ErrorFn onError_;
    std::thread worker_;
};

} // namespace backend::recording
```

- [ ] **Step 5: Build + run** — `cmake --build build --config Debug --target hdf_write_queue_test` then `ctest --test-dir build -C Debug -R backend.hdf_write_queue -V`. Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/backend/recording/HdfWriteQueue.h tests/backend/hdf_write_queue_test.cpp tests/CMakeLists.txt
git commit -m "Add bounded HdfWriteQueue (3-slot decoupled writer, fatal on overflow/failure)"
```

---

### Task 2: `FrameStore::reserveFrameBytes` + filter hot-path alloc fix

**Files:**
- Modify: `include/backend/playback/FrameStore.h`
- Modify: `src/backend/playback/FrameStore.cpp:23-52` (filter temp), add method
- Create: `tests/backend/frame_store_reserve_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `void FrameStore::reserveFrameBytes(size_t frameBytes);` — reserves every ring slot's `data` capacity to ≥ `frameBytes` so `pushFrame` of size ≤ `frameBytes` never reallocates.

- [ ] **Step 1: Write the failing test** (`tests/backend/frame_store_reserve_test.cpp`)

```cpp
// frame_store_reserve_test — reserveFrameBytes makes the push hot path
// allocation-free: after reserving, pushing frames of size <= reserved does not
// change any slot's data capacity.
#include "backend/playback/FrameStore.h"
#include "support/assert.h"
#include <vector>

using backend::playback::FrameStore;
using backend::playback::Frame;

int main() {
    const size_t cap = 8;
    const size_t bytes = 64 * 64; // 4096
    FrameStore fs(cap);
    fs.reserveFrameBytes(bytes);

    std::vector<uint8_t> src(bytes, 7);
    // Capture capacities of the slots after reservation by reading frames back
    // is not possible (data is copied out); instead, push a full ring twice and
    // assert the round-trip data is intact and no smaller-than-reserved frame
    // triggers growth (observable via availableCount staying capped + no crash).
    for (size_t i = 0; i < cap * 2; ++i) {
        fs.pushFrame(src.data(), src.size(), 64, 64, 64, 0x01080001u, i + 1);
    }
    MIB_REQUIRE(fs.totalWritten() == cap * 2, "all frames pushed");
    MIB_REQUIRE(fs.availableCount() == cap, "ring capped at capacity");

    Frame out;
    MIB_REQUIRE(fs.getByWriteIndex(cap * 2 - 1, out), "latest frame retrievable");
    MIB_EXPECT(out.data.size() == bytes && out.data[0] == 7, "frame data intact");

    // A larger-than-reserved frame must still work (grows that slot).
    std::vector<uint8_t> big(bytes * 2, 9);
    fs.pushFrame(big.data(), big.size(), 128, 64, 128, 0x01080001u, 999);
    MIB_REQUIRE(fs.getByWriteIndex(cap * 2, out), "oversize frame retrievable");
    MIB_EXPECT(out.data.size() == bytes * 2 && out.data[0] == 9, "oversize frame intact");

    if (mib::test::exitCode() == 0) std::printf("FrameStore reserveFrameBytes verified\n");
    return mib::test::exitCode();
}
```

(Note: this test verifies correctness of reserve + growth. The allocation-free property is structurally guaranteed by `reserve()` + `resize()` with size ≤ capacity and is asserted by code review; a capacity probe would require exposing internals we deliberately keep private.)

- [ ] **Step 2: Register the test** in `tests/CMakeLists.txt`:

```cmake
mib_add_backend_test_executable(frame_store_reserve_test backend/frame_store_reserve_test.cpp)
add_test(NAME backend.frame_store_reserve COMMAND $<TARGET_FILE:frame_store_reserve_test>)
set_tests_properties(backend.frame_store_reserve PROPERTIES LABELS "backend" TIMEOUT 30)
```

- [ ] **Step 3: Verify it fails** — build target → FAIL (`reserveFrameBytes` undeclared).

- [ ] **Step 4: Declare** in `FrameStore.h` (after `resize`, ~line 113):

```cpp
        // Pre-reserve each ring slot's data buffer to at least frameBytes so the
        // per-frame pushFrame hot path performs no heap allocation for frames of
        // that size or smaller. Call at capture start once geometry is known.
        void reserveFrameBytes(size_t frameBytes);
```

- [ ] **Step 5: Implement** in `FrameStore.cpp` (add after the constructor):

```cpp
void FrameStore::reserveFrameBytes(size_t frameBytes) {
    if (frameBytes == 0) return;
    std::unique_lock structLk(structureMutex_);
    for (auto& f : ring_) {
        if (f.data.capacity() < frameBytes) f.data.reserve(frameBytes);
    }
    SPDLOG_INFO("FrameStore: reserved {} bytes per slot across {} slots", frameBytes, ring_.size());
}
```

- [ ] **Step 6: Fix the filter per-frame allocation** in `FrameStore.cpp` — make the filter scratch `Frame` thread-local so its buffer is reused (replace the `Frame tmp;` block at lines 36-43):

```cpp
        if (frameFilter_) {
            // Reuse a per-thread scratch Frame so the filter inspection does not
            // allocate on every frame (assign reuses the existing capacity).
            thread_local Frame tmp;
            tmp.width = width;
            tmp.height = height;
            tmp.pixelFormat = pixelFormat;
            tmp.linePitch = linePitch;
            tmp.timestamp = timestamp;
            tmp.data.assign(src, src + size);
            if (frameFilter_(tmp)) {
```

- [ ] **Step 7: Build + run** — build `frame_store_reserve_test`; `ctest -R backend.frame_store_reserve -V`. Expected: PASS. Also run `ctest -R "frame_store" ` to keep existing FrameStore tests green.

- [ ] **Step 8: Commit**

```bash
git add include/backend/playback/FrameStore.h src/backend/playback/FrameStore.cpp tests/backend/frame_store_reserve_test.cpp tests/CMakeLists.txt
git commit -m "FrameStore: reserveFrameBytes for allocation-free hot path; reuse filter scratch"
```

---

### Task 3: Reserve the FIFO at capture start (`CaptureService`)

**Files:**
- Modify: `src/backend/services/CaptureService.cpp:139-167`

**Interfaces:**
- Consumes: `FrameStore::reserveFrameBytes` (Task 2).

- [ ] **Step 1: Reserve on first frame / geometry growth** — in the capture loop, before the `frameStore_->pushFrame(...)` call, track the reserved size and reserve when a frame's size exceeds it:

```cpp
            if (frameStore_) {
                // Pre-reserve FIFO slots to the live frame size so the high-speed
                // hot path never allocates (first reservation happens on frame 1;
                // re-reserve only if the geometry grows).
                const size_t frameBytes = frame.data.size();
                if (frameBytes > reservedFrameBytes_) {
                    frameStore_->reserveFrameBytes(frameBytes);
                    reservedFrameBytes_ = frameBytes;
                }
                frameStore_->pushFrame(frame.data.data(),
                                       frame.data.size(),
                                       frame.width,
                                       frame.height,
                                       frame.linePitch,
                                       frame.pixelFormat,
                                       frame.timestamp);
            }
```

Declare `size_t reservedFrameBytes_ = 0;` as a local just before the `while (running_.load())` loop (line ~128) so it resets per capture session.

- [ ] **Step 2: Build** — `cmake --build build --config Debug --target mib_backend`. Expected: compiles.

- [ ] **Step 3: Commit**

```bash
git add src/backend/services/CaptureService.cpp
git commit -m "CaptureService: pre-reserve FrameStore FIFO at capture start"
```

---

### Task 4: Recording via `HdfWriteQueue` + fix silent failure

**Files:**
- Modify: `include/backend/app/AppBackend.h` (recording-error callback + queue member)
- Modify: `src/backend/app/AppBackend.cpp:811-997` (recording loop + stop)

**Interfaces:**
- Consumes: `HdfWriteQueue<RecordingBatch>` (Task 1), where `struct RecordingBatch { std::vector<cv::Mat> images; std::vector<services::Hdf5Service::RecordingFrameMeta> meta; };` (define locally in the .cpp).
- Produces: `void AppBackend::setFatalSaveErrorCallback(std::function<void(const std::string&)>);` (shared by Task 5).

- [ ] **Step 1: Add the fatal-error callback** to `AppBackend.h`: a member `std::function<void(const std::string&)> fatalSaveErrorCb_;` and setter `void setFatalSaveErrorCallback(std::function<void(const std::string&)> cb);` plus a private helper `void reportFatalSaveError(const std::string& msg);` that invokes it (guarded for null).

- [ ] **Step 2: Implement** `setFatalSaveErrorCallback`/`reportFatalSaveError` in `AppBackend.cpp`:

```cpp
void AppBackend::setFatalSaveErrorCallback(std::function<void(const std::string&)> cb) {
    fatalSaveErrorCb_ = std::move(cb);
}
void AppBackend::reportFatalSaveError(const std::string& msg) {
    SPDLOG_ERROR("Fatal save error: {}", msg);
    if (fatalSaveErrorCb_) fatalSaveErrorCb_(msg);
}
```

- [ ] **Step 3: Route recording writes through the queue** — in the recording thread (`startFrameRecording`), replace the inline `appendRecordingFrames` + count logic. Construct a queue whose `writeFn` writes and, on success, advances the count; on failure returns false (the queue latches + calls `onError` → stop recording + `reportFatalSaveError`). Concretely:

```cpp
        struct RecordingBatch { std::vector<cv::Mat> images; std::vector<services::Hdf5Service::RecordingFrameMeta> meta; };
        auto writeFn = [this](const RecordingBatch& b) -> bool {
            if (!hdf5Service_->appendRecordingFrames(b.images, b.meta)) return false;
            frameRecordingWritten_.fetch_add(b.images.size(), std::memory_order_relaxed);
            return true;
        };
        auto onError = [this](const std::string& msg) {
            frameRecordingRunning_.store(false);                 // stop the collector loop
            reportFatalSaveError("Recording save failed: " + msg);
        };
        backend::recording::HdfWriteQueue<RecordingBatch> writeQueue(3, writeFn, onError);
```

Then at each "batch full" point and the final flush, replace direct append with submit:

```cpp
                    if (batchImages.size() >= FLUSH_BATCH) {
                        if (!writeQueue.submit(RecordingBatch{std::move(batchImages), std::move(batchMeta)})) {
                            break; // fatal error already surfaced via onError
                        }
                        batchImages.clear(); batchMeta.clear();
                        batchImages.reserve(FLUSH_BATCH); batchMeta.reserve(FLUSH_BATCH);
                    }
```

After the loop, drain: `if (!batchImages.empty()) writeQueue.submit(RecordingBatch{std::move(batchImages), std::move(batchMeta)}); const bool drained = writeQueue.flushAndStop(); if (!drained) reportFatalSaveError("Recording final flush failed");`. Only call `writeRecordingInfo` + `closeFile` after the drain. **Remove** the old unconditional `frameRecordingWritten_.fetch_add` and the "log-and-continue" behavior — the count now advances only inside `writeFn` on success.

- [ ] **Step 4: Build** — `cmake --build build --config Debug --target mib_backend`. Expected: compiles.

- [ ] **Step 5: Commit**

```bash
git add include/backend/app/AppBackend.h src/backend/app/AppBackend.cpp
git commit -m "Recording: route writes through HdfWriteQueue; stop+report on failure (no silent loss)"
```

---

### Task 5: Experiment flush via `HdfWriteQueue` + stop-on-error

**Files:**
- Modify: `include/backend/processing/ProcessingService.h`
- Modify: `src/backend/processing/ProcessingService.cpp:1085-1160` (flush) + start/stop

**Interfaces:**
- Consumes: `HdfWriteQueue<ExperimentBatch>` where `struct ExperimentBatch { std::vector<ProcessedFrame> valid; std::vector<ProcessedFrame> invalid; };`
- Produces: `void ProcessingService::setFlushErrorCallback(std::function<void(const std::string&)>);` and an owned queue created in `startExperiment`, torn down (flushAndStop) in `stopExperiment`/final flush.

- [ ] **Step 1: Own a queue for the experiment** — in `ProcessingService.h` add `std::unique_ptr<backend::recording::HdfWriteQueue<ExperimentBatch>> flushQueue_;` (forward-declare batch), a `Hdf5Service*` captured at experiment start for the writeFn, and `std::function<void(const std::string&)> flushErrorCb_;` with a setter.

- [ ] **Step 2: Create the queue in the flush driver** — change `flushBufferedFrames(Hdf5Service& hdf5)` so that instead of writing inline it moves the buffers into an `ExperimentBatch` and `submit()`s to a queue bound to `hdf5`. Lazily create `flushQueue_` on first flush (writeFn calls `hdf5.appendFrames`, success advances `totalValidFlushed_`; onError calls `flushErrorCb_` + marks the experiment failed). On `submit()==false`, return 0 and surface the error (already fired via onError). Keep the move-out-under-lock so capture never blocks:

```cpp
size_t ProcessingService::flushBufferedFrames(class Hdf5Service& hdf5) {
    ExperimentBatch batch;
    {
        std::scoped_lock lk(framesMutex_);
        if (validFrames_.empty() && invalidFrames_.empty()) return 0;
        batch.valid = std::move(validFrames_);  validFrames_.clear();
        batch.invalid = std::move(invalidFrames_); invalidFrames_.clear();
    }
    const size_t n = batch.valid.size() + batch.invalid.size();
    if (!flushQueue_) {
        auto writeFn = [this, &hdf5](const ExperimentBatch& b) -> bool {
            if (!hdf5.appendFrames(b.valid, b.invalid)) return false;
            totalValidFlushed_.fetch_add(b.valid.size(), std::memory_order_relaxed);
            return true;
        };
        auto onError = [this](const std::string& msg) { if (flushErrorCb_) flushErrorCb_("Experiment save failed: " + msg); };
        flushQueue_ = std::make_unique<backend::recording::HdfWriteQueue<ExperimentBatch>>(3, writeFn, onError);
    }
    if (!flushQueue_->submit(std::move(batch))) return 0; // fatal already surfaced
    return n;
}
```

- [ ] **Step 3: Drain on stop** — in the experiment stop/final-flush path (`ExperimentController::stopExperiment` calls `flushBufferedFrames` then appends remainder), add a `ProcessingService::finishFlush()` that, if `flushQueue_`, calls `flushAndStop()` and returns success, then resets the queue. Call it before `writeExperimentInfo`. (Declare `bool finishFlush();`.)

- [ ] **Step 4: Build** — `cmake --build build --config Debug --target mib_backend`. Expected: compiles.

- [ ] **Step 5: Commit**

```bash
git add include/backend/processing/ProcessingService.h src/backend/processing/ProcessingService.cpp
git commit -m "Experiment flush: route through HdfWriteQueue; stop+report on failure"
```

---

### Task 6: Surface fatal save errors in the UI

**Files:**
- Modify: `src/backend/app/AppBackend.cpp` (wire ProcessingService flush-error cb to AppBackend's reporter)
- Modify: `src/frontend/core/MainWindow.cpp` + `.h` (connect + dialog + stop)

**Interfaces:**
- Consumes: `AppBackend::setFatalSaveErrorCallback` (Task 4), `ProcessingService::setFlushErrorCallback` (Task 5).

- [ ] **Step 1: Bridge ProcessingService → AppBackend** — in `AppBackend` init, `processingService_->setFlushErrorCallback([this](const std::string& m){ reportFatalSaveError(m); });` so both recording and experiment errors funnel through `reportFatalSaveError`.

- [ ] **Step 2: Bridge AppBackend → Qt** — in `MainWindow` setup, `backend_.setFatalSaveErrorCallback(...)` posting to the UI thread (the callback fires on a writer thread). Use `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)` to show the dialog + stop on the UI thread:

```cpp
    backend_.setFatalSaveErrorCallback([this](const std::string& msg) {
        const QString q = QString::fromStdString(msg);
        QMetaObject::invokeMethod(this, [this, q]() {
            // Stop whichever operation is active.
            if (backend_.isFrameRecording()) backend_.stopFrameRecording();
            if (experimentController_ && experimentController_->isActive()) experimentController_->stopExperiment(nullptr);
            statusBar()->showMessage(tr("Save error: %1").arg(q));
            QMessageBox::critical(this, tr("Save Error"),
                tr("Data could not be saved and the operation was stopped:\n\n%1").arg(q));
        }, Qt::QueuedConnection);
    });
```

(Confirm the exact `experimentController_` accessor / `isActive()` at implementation time; if recording-only, omit the experiment branch.)

- [ ] **Step 3: Build** — `cmake --build build --config Debug --target mib_studio_qt`. Expected: compiles.

- [ ] **Step 4: Commit**

```bash
git add src/backend/app/AppBackend.cpp src/frontend/core/MainWindow.cpp include/frontend/core/MainWindow.h
git commit -m "Surface fatal save errors: stop the operation + modal dialog"
```

---

### Task 7: Vault + docs

**Files:**
- Modify: `knowledge_map/data-model/FrameStore.md`, `knowledge_map/services/ProcessingService.md`, `knowledge_map/architecture/AppBackend.md`
- Create: `knowledge_map/recording/HdfWriteQueue.md` (or place under an existing recording MOC)

- [ ] **Step 1: Document** `reserveFrameBytes` + allocation-free hot path (FrameStore), `HdfWriteQueue` (new note), and stop-on-error recording/experiment flush (ProcessingService/AppBackend).
- [ ] **Step 2: Validate** — `python scripts/check_docs.py` → `knowledge base OK`.
- [ ] **Step 3: Commit**

```bash
git add knowledge_map/
git commit -m "Docs/vault: HdfWriteQueue, FrameStore reservation, stop-on-error flush"
```

---

## Self-Review

**Spec coverage:** §1 HdfWriteQueue → Task 1; §2 FIFO reservation → Tasks 2+3; §3 recording → Task 4; §4 experiment flush → Task 5; §5 error surfacing → Tasks 4/5 callbacks + Task 6 UI; testing → Tasks 1/2 + recording regression folded into Task 4 build (note: the recording silent-failure regression is exercised by the HdfWriteQueue failure test + the writeFn-only count change); vault → Task 7. Covered.

**Placeholder scan:** Task 6 defers the exact `experimentController_`/`isActive()` accessor to implementation (must be read from live code) and Task 5 step 3 references the stop path — both specify behavior fully; no vague logic. Acceptable.

**Type consistency:** `HdfWriteQueue<Batch>`, `submit`, `flushAndStop`, `hasError`, `WriteFn`/`ErrorFn` consistent across Tasks 1/4/5. `reserveFrameBytes(size_t)` consistent Tasks 2/3. `RecordingBatch{images,meta}` / `ExperimentBatch{valid,invalid}` named consistently within their tasks. `setFatalSaveErrorCallback` / `reportFatalSaveError` / `setFlushErrorCallback` consistent across Tasks 4/5/6.
