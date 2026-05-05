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

## Pipeline (per frame)

1. Optional background subtraction (`setRealtimeBackgroundGray`).
2. Segment ROI/frame mask via either:
   - classical path (Gaussian blur → threshold → morphology), or
   - optional lightweight U-Net callback (`use_lightweight_unet=true`).
   If U-Net inference is unavailable/fails, the classical path is used.
3. `filterProcessedImage` produces a `FilterResult`:
   - `deformability`, `area` (μm² via `pixelToMicronFactor_`),
     `areaRatio`, `ringRatio`, `youngsModulus` (LUT lookup)
   - `brightness` quantiles (Q1/Q2/Q3/Q4)
   - border check, single-inner-contour check, range gates
   - `isTargetGroup` (second gate for trigger-worthy frames)
4. Emits:
   - **Ring ratio** via `RingRatioCallback` → [[AutofocusService]].
   - **Target group** bool via `TargetGroupCallback` → [[TriggerService]].
   - **Background capture** (if auto-background enabled) via
     `BackgroundCaptureCallback` → UI notifier.

## Config — `ProcessingConfig`

All gates in one struct. Notable fields:

- `area_threshold_min/max` (μm²), `deformability_threshold_min/max`
- `ring_ratio_min/max` + `enable_ring_ratio_check`
- `empty_frame_pixel_threshold` — drives empty-frame skipping
- `auto_background_enabled` + `auto_background_empty_frames`,
  `auto_background_cooldown_frames`
- Target-group gate: `target_group_area_*`, `target_group_deformability_*`,
  `enable_target_group_emodulus` + `target_group_emodulus_*` (uses
  `EModulusLut`)
- Multi-image mode: `multi_image_enabled`, `multi_image_count`
- Optional lightweight U-Net inference path:
  `use_lightweight_unet`, `lightweight_unet_threshold`

## Accumulation modes

- **Monitoring rings** — `monitoringValidFrames_` / `monitoringInvalidFrames_`,
  fixed 1000-frame capacity. Always on when realtime is running.
- **Experiment accumulation** — unbounded vectors; populated while
  `experimentActive_` is true. Flushed to HDF5 periodically via
  `flushBufferedFrames(Hdf5Service&)` (default every 100 frames).
- Invalid frame sampling rate defaults to 1-in-100 to bound HDF5 size.

## Metrics exposed

1-second rolling window: `getAlgoFps1s`, `getValidFps1s`, `getInvalidFps1s`,
`getAlgoAvgUs1s`. Plus `getTotalValidFlushed` for experiment totals.

## Batch processing (offline / re-runs)

For re-generating masks from stream images that are **not** coming from a
live camera — e.g., frames stored in an HDF5 experiment file or a folder of
TIFFs — use:

- `ProcessingService::computeProcessedFrame(grayInput, background, config, roi, index, ts)`
  — pure helper: optional U-Net mask inference (or blur → background subtract
  → threshold → morphology fallback) → `filterProcessedImage`. Zero side-effects (no monitoring
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

## Gotchas

- Realtime drop-frames mode is ignored while an experiment is active.
- `pixelToMicronFactor_` default is `0.4886` — UI lets users change this.
- Optional U-Net segmentation path is callback-driven (`setSegmentationMaskCallback`)
  and wired from [[YoloService]] in `AppBackend`.
- **Callback ordering invariant**: `TargetGroupCallback` and
  `RingRatioCallback` are invoked **before** `monitoringFramesMutex_` is
  taken (and with no other locks held) so the UI thread's periodic
  `getMonitoringValidFrames()` / `getMonitoringInvalidFrames()` snapshot
  cannot stall the [[TriggerService]] wake-up. Do not move these callback
  calls back inside the monitoring-mutex scope; hold-time there directly
  becomes trigger-onset jitter (see 2026-04-15 task record).
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
