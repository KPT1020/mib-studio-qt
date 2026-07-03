# ProcessingService

> The heart of the analysis pipeline. Runs OpenCV-based detection, classifies
> frames valid/invalid, accumulates monitoring + experiment buffers, feeds
> ring-ratio to [[AutofocusService]] and target-group events to
> [[TriggerService]].

**Source:** `src/backend/services/ProcessingService.cpp`,
`include/backend/services/ProcessingService.h`
**Related:** [[CaptureService]], [[Hdf5Service]], [[AutofocusService]],
[[TriggerService]], [[../architecture/Data-Flow]],
[[../domain/Microscopy-Pipeline]]

## Threads

- **Worker pool** (`start(size_t n = hardware_concurrency())`) — generic
  `Job` queue (not heavily used at present; realtime loop carries most work).
- **Realtime thread** (`startRealtime(frameStore)`) — polls FrameStore
  write-index; processes every frame or only latest depending on
  `setRealtimeDropFrames`. Experiments force every-frame.
- **Async batch workers** (`startBatchPipeline`) — consume a bounded frame
  queue in configured-size batches. `enqueueBatchFrame` returns immediately
  with accepted/dropped status so capture can keep running while workers process
  behind it.

The destructor is self-sufficient: it calls `stopRealtime()` +
`stopBatchPipeline()` + `stop()`, so destroying the service with any thread
still live is safe (previously a joinable `realtimeThread_` at destruction
`std::terminate`d unless GUI teardown had called `stopRealtime()` first).
`isRealtimeRunning()` exposes the realtime thread state.

All three thread families contain exceptions instead of letting them escape
the thread entry function (which is `std::terminate`): worker jobs and batch
compute/callback sections log-and-drop the failing job/batch;
`realtimeLoop()` catches, cleans up the batch pipeline, sleeps 100 ms, and
restarts the loop (same policy as `CaptureService::run`). Verified by
`tests/processing/processing_fault_injection_test.cpp`.

## Pipeline (per frame)

1. Optional background subtraction (`setRealtimeBackgroundGray`).
2. Gaussian blur → threshold → morphological ops → contour find.
   - Thresholding goes through `applyProcessingThreshold` (shared by
     `computeProcessedFrame` and all realtime-loop copies). Default: fixed
     `bg_subtract_threshold`. When `config.adaptive_threshold` is set, it uses
     per-frame **Otsu**, floored at `bg_subtract_threshold` (so near-empty ROIs
     stay empty) and scaled by `otsu_scale`. The `isFrameEmpty` gates keep the
     fixed threshold. Evidence + tuning: `benchmarks/mask-gen/REPORT.md`;
     regression/invariant coverage in
     `tests/processing/processing_adaptive_threshold_test.cpp`.
3. `filterProcessedImage` produces a `FilterResult`:
   - `deformability`, `area` (μm² via `pixelToMicronFactor_`),
     `areaRatio`, `ringRatio`, `youngsModulus` (LUT lookup)
   - `brightness` quantiles (Q1/Q2/Q3/Q4)
   - border check, single-inner-contour check, range gates
   - `isTargetGroup` (second gate for trigger-worthy frames)
4. Emits:
   - **Ring ratio** via `RingRatioCallback` → [[AutofocusService]].
   - **Target-group** event via `TargetGroupCallback` carrying owner identity
     (`objectId`, `trackId`) → [[TriggerService]].
   - **Background capture** (if auto-background enabled) via
     `BackgroundCaptureCallback` → UI notifier.

## Background accessor — zero-copy hot path

Two getters exist for the current background:

- `cv::Mat getRealtimeBackgroundGray()` — clones the background (safe for
  cold callers, e.g. `MainWindow`, `BufferSaveDialog`).
- `std::shared_ptr<const cv::Mat> getRealtimeBackgroundGrayShared()` — returns
  the shared_ptr directly (no clone). Use in hot paths where you only need to
  read the background. The pointed-to Mat is immutable; `setRealtimeBackgroundGray`
  always replaces the pointer atomically rather than mutating in place.

`getConfigVersion()` returns a monotonic counter bumped by `setProcessingConfig`
and `setRealtimeRoi`. Hot loops can compare against a cached version to skip
per-frame config reads.

`isFrameEmpty(frame, config, roi, shared_ptr<const cv::Mat>)` — ROI-only
overload: extracts only the ROI pixels (no full-frame copy) and uses the
background directly via shared_ptr. Classifies identically to the full-frame
overload (`recording_isframeempty_roi_test` is the invariant test).

## Config — `ProcessingConfig`

All gates in one struct. Notable fields:

- `area_threshold_min/max` (μm²), `deformability_threshold_min/max`
- `ring_ratio_min/max` + `enable_ring_ratio_check`
- `bg_subtract_threshold` — fixed segmentation threshold (also the Otsu floor)
- `adaptive_threshold` (default off) + `otsu_scale` — per-frame Otsu segmentation
- `empty_frame_pixel_threshold` — drives empty-frame skipping
- `auto_background_enabled` + `auto_background_empty_frames`,
  `auto_background_cooldown_frames`
- Target-group gate: `target_group_area_*`, `target_group_deformability_*`,
  `enable_target_group_emodulus` + `target_group_emodulus_*` (uses
  `EModulusLut`, which is now fed from the managed LUT cache prepared by
  `AppBackend` at startup)
- Multi-image mode: `multi_image_enabled`, `multi_image_count`

## Accumulation modes

- **Monitoring rings** — `monitoringValidFrames_` / `monitoringInvalidFrames_`,
  fixed 1000-frame capacity. **Gated** by `setMonitoringActive(bool)` (default
  off). When inactive, `appendRealtimeMonitoringFrame` returns immediately with
  no allocations. Wired to [[../frontend/ExperimentMonitoringTab]] show/hide —
  the rings only fill while that tab is visible. Stored frames share cv::Mat
  refcounts with the processing loop (no per-frame clone); consumers are
  read-only.
- **Experiment accumulation** — bounded `std::deque<ProcessedFrame>` populated
  while `experimentActive_` is true. Deque gives O(1) `pop_front()` when the
  bounded backlog is full under high frame rates. `flushBufferedFrames(Hdf5Service&)`
  moves frames into an `ExperimentBatch` via `std::make_move_iterator` (O(1)
  per Mat, refcount transfer), submits to a 3-slot [[Hdf5Service]] `HdfWriteQueue`
  whose writer thread does the slow append, so capture/processing never blocks
  on disk. The write queue is created lazily on first flush and torn down by
  `finishFlush()` at experiment stop (drains + joins before any direct HDF5
  write, so the file is never written by two threads at once). A write failure
  or queue overflow (disk too slow) is fatal: it fires `setFlushErrorCallback`
  (→ [[../architecture/AppBackend]] → UI stop + dialog) instead of the old
  silent trim-and-drop. `totalValidFlushed_` advances only on a confirmed write.
- **Frozen-Mats invariant**: every `cv::Mat` published from `realtimeInlineLoop`
  (`gray`, `mask`, `grayROI`, `grayFull`, `fullMask`) is freshly allocated per
  iteration and never written after publication. Experiment frames and monitoring
  ring entries share refcounts instead of cloning (PR3 clone elimination).
  Consumers (`Hdf5Service::appendFrames`, monitoring/HDF readers, overlay) are
  all read-only. Enforced by comment at the top of `realtimeInlineLoop`.
- Invalid frame sampling rate defaults to 1-in-100 to bound HDF5 size.

## Snapshot model (PR4)

`latestSnapshot_` is `std::shared_ptr<const RealtimeSnapshot>`. Producers
(`realtimeInlineLoop`, `publishRealtimeBatchFrame`) build a new `RealtimeSnapshot`
outside `snapshotMutex_`, then pointer-swap inside the lock (O(1) hold time —
no full-frame memcpy under the mutex). Consumers call `getLatestSnapshot(out)`,
which copies the shared_ptr under the lock (O(1)) and reads fields outside.
`out.mask` is a shallow refcount share of the snapshot mask — no clone on the
read path. Snapshot is immutable; readers are safe without extra locking.

`configVersion_` is also bumped by `setRealtimeBackgroundGray` (in addition to
`setProcessingConfig` / `setRealtimeRoi`), so the realtime loop's hoisted-config
cache refreshes when the background changes.

## Metrics exposed

1-second rolling window: `getAlgoFps1s`, `getValidFps1s`, `getInvalidFps1s`,
`getAlgoAvgUs1s`. Plus `getBufferedFrameCounts()` for cheap UI/status polling,
`getTotalValidFlushed` for experiment totals, and dropped-frame counters for
the bounded experiment backlog.

## Batch processing (offline / re-runs)

For re-generating masks from stream images that are **not** coming from a
live camera — e.g., frames stored in an HDF5 experiment file or a folder of
TIFFs — use:

- `ProcessingService::computeProcessedFrame(grayInput, background, config, roi, index, ts)`
  — pure helper: blur → (optional) background subtract → threshold →
  morphology → `filterProcessedImage`. Zero side-effects (no monitoring
  rings, no experiment accumulation, no callbacks, no auto-background).
  Returns a `ProcessedFrame` with a full-size mask (zero outside ROI).
- `ProcessingService::processBatch(grayImages, config, background, roi, progressCb)`
  — wraps `computeProcessedFrame` over a vector of grayscale `cv::Mat`
  inputs, returns `std::vector<ProcessedFrame>`. Progress is reported via
  `BatchProgressCallback(BatchProgress{done, total})`.

Input/output adapters live in [[BatchMaskSources]] —
`loadFromHdf5` / `loadFromFolder` (input), `saveMaskImages` /
`saveMasksToHdf5` (output). `HdfReviewTab` exposes a "Regenerate masks…"
button that drives this via `BatchMaskDialog`.

Batch calls do **not** touch realtime state or monitoring buffers, so
they're safe to run concurrently with live capture.

## Async batch processing (capture decoupling)

Use `ProcessingService::startBatchPipeline(config, callback)` when capture
should only enqueue frames and let workers process them later. The config
contains `batchSize`, `maxQueuedFrames`, `workerCount`, and the same
`ProcessingConfig` / background / ROI inputs used by `computeProcessedFrame`.

`enqueueBatchFrame(gray, index, timestampNs)` clones the frame into the bounded
queue and returns:

- `true` when the frame was accepted,
- `false` when the pipeline is stopped, the frame is empty, or the queue is
  already full.

Workers wait for `batchSize` queued frames, process with
`computeProcessedFrame`, and emit `std::vector<ProcessedFrame>` through
`BatchResultCallback`. `stopBatchPipeline` drains residual partial batches
before joining workers.

`getBatchPipelineStats()` exposes accepted, dropped, processed, batch count,
current/max queue depth, batch size, worker count, and running state. See
`docs/batch_pipeline_architecture.md` for the migration plan from
`FrameStore -> realtimeLoop` to capture enqueue -> batch worker.

## Gotchas

- Realtime drop-frames mode is ignored while an experiment is active.
- **Live-view overlay backlog / `rtDropFrames_` defaults ON:** the inline
  realtime loop consumes `FrameStore` sequentially via `rtLastProcessed_`. If
  drop-frames is *off* and capture outpaces processing, the processed snapshot
  (`getLatestSnapshot`, source of the mask/contour overlay and trigger decision)
  falls progressively behind the live write head — an accumulating backlog that
  only resets when realtime restarts. The raw preview is unaffected (it reads
  `FrameStore::getLatest`). Because of this, `rtDropFrames_` now **defaults to
  ON**, so the live overlay jumps to the newest frame and stays bounded.
  Experiments are unaffected: the loop gates the flag behind `!experimentActive_`
  (`rtDropFrames_ && !experimentActive_`), so every frame is still
  processed/recorded during a run. Users can still disable it via
  ProcessingSettingsDialog. Verified by
  `tests/processing/realtime_drop_frames_default_test.cpp` (default value +
  toggle) and `tests/integration/e2e_live_view_latency_test.cpp`
  (`integration.e2e_live_view_latency`): under sustained overload the default
  stays a few hundred frames behind, vs. tens of thousands with drop-frames
  forced off (~30x).
- When the experiment backlog reaches `maxBufferedFrames_`, sampled invalid
  frames are dropped first. Valid frames can evict old invalid frames; valid
  drops only happen if the backlog is entirely valid and still over cap. This
  is a last-resort RAM safety valve for long runs where HDF5 is slow or failing.
- `pixelToMicronFactor_` default is `0.4886` — UI lets users change this.
- YOLO is a separate service ([[YoloService]]); this pipeline does not use it.
- **Callback ordering invariant**: `TargetGroupCallback` and
  `RingRatioCallback` are invoked **before** `monitoringFramesMutex_` is
  taken (and with no other locks held) so the UI thread's periodic
  `getMonitoringValidFrames()` / `getMonitoringInvalidFrames()` snapshot
  cannot stall the [[TriggerService]] wake-up. Do not move these callback
  calls back inside the monitoring-mutex scope; hold-time there directly
  becomes trigger-onset jitter (see 2026-04-15 task record).
- **Target-group ownership policy**: after each frame's detections are
  evaluated, at most one `TargetGroupCallback` is emitted per frame, by
  taking the first target-group object in deterministic contour-sort order
  (the same order used for `objectId` assignment). This is the source object
  for that frame's trigger request.
- **Callback order: target-group THEN ring-ratio.** Within the hoisted
  callback block, `TargetGroupCallback` fires **first** so the
  [[TriggerService]] condition variable is notified before
  `RingRatioCallback`. As of 2026-04-16 `AutofocusService::onRingRatio`
  is O(1) (push into an inbox + atomic updates + `notify_one`) with the
  sort / deque trim moved onto a dedicated stats thread, so this ordering
  is no longer strictly required — but target-group first is still the
  safest invariant in case the ring-ratio callback target changes.
- Per-frame `previousFrameForAutoCapture_` assignment uses cv::Mat's
  refcounted shallow copy (`= blurredCurr`, **not** `.clone()`). Cloning
  every non-empty frame was an allocator-pressure source for algo-time
  variance when auto-background was enabled.
- `computeProcessedFrame` intentionally omits the auto-background /
  previous-frame-diff path used in `realtimeLoop()`. Callers needing that
  should continue to drive frames through `FrameStore` + `startRealtime`.
- Young's modulus gating still depends on the LUT path loaded during backend
  startup; if the managed cache cannot be updated, the pipeline keeps using
  the last known-good local copy or the bundled fallback.
- `FilterResult::allContours` is a `shared_ptr<const ...>`, **not** a value.
  All per-object `FilterResult`s of a frame share one allocation (assigned
  once by `filterProcessedObjects` after evaluation), so the monitoring /
  experiment copies are refcount bumps rather than deep copies of every
  contour point. Consumers must deref (`*validation.allContours`) and
  null-check. The write-only `hierarchy` field was removed — nothing read it.
- `calculateBrightnessQuantiles` takes an optional bbox `region`: the per-
  object evaluators pass the object's bounding box so the scan only covers
  pixels that can be non-zero in that object's mask (identical sample set,
  not the whole ROI). It also uses row pointers instead of `cv::Mat::at<>`
  and skips the `clone()` for already-single-channel input. These were
  per-object allocator/CPU costs that scaled with objects-per-frame.
