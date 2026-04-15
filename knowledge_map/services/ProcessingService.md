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
2. Gaussian blur → threshold → morphological ops → contour find.
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

## Gotchas

- Realtime drop-frames mode is ignored while an experiment is active.
- `pixelToMicronFactor_` default is `0.4886` — UI lets users change this.
- YOLO is a separate service ([[YoloService]]); this pipeline does not use it.
