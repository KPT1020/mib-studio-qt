# Data Flow

> Camera → FrameStore → Processing → (HDF5 | Autofocus | Trigger | UI)

**Related:** [[Threading-Model]], [[AppBackend]],
[[../data-model/FrameStore]], [[../services/ProcessingService]]

## Realtime path (no experiment active)

```
┌───────────┐    grabFrame()     ┌──────────────┐  pushFrame()  ┌────────────┐
│  ICamera  ├───────────────────▶│CaptureService├──────────────▶│ FrameStore │
└───────────┘                    └──────┬───────┘               └──────┬─────┘
                                        │ FrameCallback                │ writeIndex
                                        ▼                              │
                                 UI live preview                       │
                                 (PreviewPage)                         │
                                                                       │
                               ┌───────────────────────────────────────┘
                               ▼
                     ┌───────────────────┐
                     │ ProcessingService │
                     │  realtimeLoop()   │──▶ Ring ratio ─▶ AutofocusService
                     └─────────┬─────────┘
                               │
                   target-group result
                               │
                               ▼
                       TriggerService ──▶ camera digital output pulse
```

## Experiment path

While an experiment is active
([[../frontend/Controllers]] `ExperimentController::State::Active`):

1. Every frame is processed (realtime drop-frames mode is ignored).
2. Valid + invalid `ProcessedFrame` objects accumulate in-memory up to
   `ProcessingService`'s bounded experiment backlog.
3. Every `flushInterval` frames (default 100),
   `flushBufferedFrames(hdf5)` drains accumulated frames to HDF5 via
   `Hdf5Service::appendFrames`.
4. Invalid frames are sampled: one in every `invalidFrameSamplingRate`
   (default 100) is retained to bound file size.
5. If HDF5 is slow or failing and the backlog reaches its cap, sampled invalid
   frames are dropped before valid frames to avoid unbounded RAM growth.
6. On stop, any remaining accumulated frames are flushed and
   `writeExperimentInfo` / `writeConfigJson` record metadata.

## Monitoring path

Runs in parallel with or without an active experiment. The service maintains
two 1000-frame ring buffers (valid, invalid). [[../frontend/ExperimentMonitoringTab]]
reads these on a timer to refresh live histograms and scatter plots.

## Key frame data types

- `camera::common::Frame` — camera-side; PFNC pixel format, linePitch,
  raw bytes. Defined in `include/backend/camera/common/Frame.h`.
- `backend::playback::Frame` — ring-buffer-side; same shape, lives in
  [[../data-model/FrameStore]].
- `backend::services::ProcessedFrame` — processing-side; `cv::Mat
  originalImage`, `cv::Mat processedImage` (mask), `FilterResult`, optional
  `seriesImages` for multi-image mode.

## ROI

`ProcessingService::setRealtimeRoi(Roi)` configures a sub-rectangle used by
both preview and analysis. ROI is persisted via
[[../frontend/System-Utilities]] `AppConfigWatcher`.
