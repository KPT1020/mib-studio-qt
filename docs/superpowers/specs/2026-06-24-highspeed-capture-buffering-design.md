# High-speed capture buffering + save-error surfacing — design

**Date:** 2026-06-24
**Status:** Approved (brainstorming)

## Problem

Operating a high-speed camera, three pipeline weaknesses cause dropped frames
and silent data loss:

1. **Experiment flush is slow / conflicts with capture.** A slow HDF5 write
   stalls the producer; the experiment path trims-and-drops buffered frames once
   it exceeds a RAM cap, silently losing data under slow disk.
2. **Recording stalls and fails silently.** `AppBackend::startFrameRecording`
   writes each batch synchronously on the collector thread, so a slow write
   stops it reading `FrameStore` → ring eviction → dropped frames. Worse, on a
   failed `appendRecordingFrames` it logs an error but still increments the
   written count and clears the batch (`AppBackend.cpp:933-940`) — silent loss
   with a wrong count and no user-facing error.
3. **The FIFO allocates on the hot path.** `FrameStore::pushFrame` `resize()`s
   each ring slot's `data` vector; after the ring fills once it is
   allocation-free, but the **first pass allocates `capacity` buffers during
   live capture**, causing startup jitter/drops on a high-speed stream. The
   filter path also allocates a temp `Frame` per frame.

Goals: decouple disk-writing from capture so writes never stall the producer;
reserve FIFO + write buffers up front so the per-frame hot path does zero heap
allocation; and surface save failures (stop + alert) instead of losing data
silently.

Decisions (from brainstorming):
- On any save failure **or** sustained buffer overflow: **stop the
  experiment/recording and surface a prominent error** — never silent loss,
  never trust a partial file.
- Buffering uses a bounded **3-slot** round-robin between producer and writer.

Non-goals: changing the HDF5 file format; concurrent experiment+recording (they
remain mutually exclusive on the shared file); delta/patch logic.

## Architecture overview

```
Camera ─> CaptureService ─push─> FrameStore (FIFO, pre-reserved slots)
                                      │
   ┌──────────────────────────────────┴───────────────────────────┐
   │ experiment flush (ProcessingService)      recording (AppBackend)│
   │   move valid/invalid batch                 collect batch        │
   └──────────────┬───────────────────────────────┬────────────────┘
                  ▼                                ▼
            HdfWriteQueue<Batch>  (bounded 3 slots + dedicated writer thread)
                  │  writeFn = Hdf5Service::appendFrames / appendRecordingFrames
                  ▼
            success: advance counts   |  failure/overflow: latch Error -> stop + callback
```

Producer (capture/collector) never blocks on HDF5: it hands a full batch to the
queue and takes a free slot. The writer thread drains slots FIFO. With all 3
slots in flight, a further `submit` latches a fatal error (disk can't keep up).

## Components

### 1. `HdfWriteQueue<Batch>` (new, pure, testable)

`include/backend/recording/HdfWriteQueue.h` (template, header-only or with a
small `.cpp`). No HDF5/Qt dependency — the write is injected.

- Construction: `HdfWriteQueue(size_t slots /*=3*/, std::function<bool(const Batch&)> writeFn, std::function<void(const std::string&)> onError)`.
- `bool submit(Batch&& batch)` — non-blocking. Returns `false` (and latches
  Error) if already in Error or all `slots` batches are in flight (overflow).
- Dedicated writer thread pops FIFO, calls `writeFn`; on `false` → latch Error
  (store message), invoke `onError` once, stop accepting work.
- `bool hasError() const; std::string error() const;`
- `void flushAndStop()` — drain queued batches then join the writer (used for a
  clean stop); returns whether all drained without error.
- Backpressure = bounded slot count; overflow is treated as a fatal save error
  per the brainstorming decision.

Testable with `Batch=int` and a mock `writeFn`: FIFO order, overflow→error,
write-failure→error+`onError`-once, `flushAndStop` drains, no work after Error.

### 2. FIFO pre-reservation (`FrameStore`)

- Add `void reserveFrameBytes(size_t frameBytes)` — under `structureMutex_`
  (exclusive), `ring_[i].data.reserve(frameBytes)` for every slot so the first
  pass through the ring performs no allocation. Idempotent; a larger request
  grows, a smaller one is a no-op.
- `CaptureService` calls it at capture start once the frame geometry is known
  (from camera config or the first frame): `frameBytes = linePitch * height`
  (fallback `width * height`). Re-reserve if geometry changes.
- Remove the per-frame temp-`Frame` allocation in the filter path: run the
  filter against the already-copied slot instead of a freshly-assigned `tmp`
  (or reuse a thread-local scratch buffer), so a set filter does not allocate
  per frame.

### 3. Recording path (`AppBackend::startFrameRecording`)

- The collector thread fills `Batch{vector<cv::Mat>, vector<RecordingFrameMeta>}`
  (capacities `reserve(FLUSH_BATCH)` once) and `submit()`s to an
  `HdfWriteQueue` whose `writeFn` calls `hdf5Service_->appendRecordingFrames`.
- The written-count increment moves into the **writer success path only**.
- On `submit()==false` / queue Error: stop recording, set the recording-error
  message, and fire the fatal-error callback. Removes the silent
  count-and-drop-on-failure bug.
- `stopFrameRecording` calls `flushAndStop()` before `writeRecordingInfo` +
  `closeFile`, and reports if the final drain errored.

### 4. Experiment flush (`ProcessingService`)

- `flushBufferedFrames` moves the valid/invalid vectors into a
  `Batch{vector<ProcessedFrame> valid, invalid}` and `submit()`s to an
  `HdfWriteQueue` whose `writeFn` calls `hdf5.appendFrames`. The success path
  updates `totalValidFlushed_`.
- Overflow / write failure → latch error → stop the experiment + fire the fatal
  callback, replacing the silent trim-and-drop under slow disk
  (`trimExperimentBuffersLocked` is no longer the slow-disk path; it remains
  only as a safety cap if needed).
- The periodic driver (today `QtConcurrent::run` guarded by `flushInProgress_`)
  becomes "move + submit" (cheap), since the writer thread owns the slow write.

### 5. Error surfacing (no silent failure)

- A backend fatal-save-error sink: a `std::function<void(const std::string&)>`
  set on `AppBackend`, invoked by either queue's `onError`. `AppBackend` bridges
  it to the UI via the existing cross-thread notifier pattern
  ([[BackgroundCaptureNotifier]]-style) → a Qt signal.
- `MainWindow` connects the signal: stop the active experiment/recording, show a
  modal error dialog (path + message), and set a status-bar error. The
  pipeline stops rather than continuing on a partial/failed file.

## Error handling

- Write failure → latched Error, one `onError`, writer stops; producer's next
  `submit` returns false so it also stops. No further frames counted as saved.
- Overflow (3 in flight) → same fatal path (disk too slow).
- Clean stop → `flushAndStop` drains; if a drain write fails, the stop reports
  the error to the user.
- Pre-reservation failure (bad_alloc on reserve) → surfaced at capture start as
  a fatal error before capture begins (fail fast, not mid-stream).

## Testing

- **`tests/backend/hdf_write_queue_test.cpp`** (mock `writeFn`, `Threads`):
  FIFO drain order; overflow→error; write-failure→error+`onError`-once;
  `flushAndStop` drains remaining; no work accepted after Error; counts only on
  success.
- **`tests/backend/frame_store_reserve_test.cpp`**: after `reserveFrameBytes(n)`,
  a burst of `pushFrame` of size ≤ n does not change any slot's `data` capacity
  (allocation-free hot path); larger frames grow as expected.
- **Recording silent-failure regression**: a failing `writeFn` makes recording
  stop and report, and the written count does **not** advance for the failed
  batch (guards the `AppBackend.cpp:933-940` bug).
- Keep existing FrameStore/recording/experiment tests green.

## Vault / docs

- `knowledge_map/data-model/FrameStore.md` — `reserveFrameBytes`, hot-path
  allocation note.
- `knowledge_map/services/ProcessingService.md` and the AppBackend recording
  note — `HdfWriteQueue`, stop-on-error, written-count-on-success.
- New note for `HdfWriteQueue` under `knowledge_map/`.

## Build sequence

1. `HdfWriteQueue` + unit test (pure; no integration) — lands first.
2. `FrameStore::reserveFrameBytes` + filter-path alloc fix + test.
3. Wire `reserveFrameBytes` into `CaptureService` start.
4. Recording path → `HdfWriteQueue` + fix silent-failure + error callback.
5. Experiment flush → `HdfWriteQueue` + stop-on-error.
6. `AppBackend` fatal-error sink → Qt signal → `MainWindow` dialog/stop.
7. Vault/docs.
