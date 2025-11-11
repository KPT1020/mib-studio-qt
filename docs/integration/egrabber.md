# Euresys EGrabber Integration

This document captures how we integrate with Euresys EGrabber and how we configure/measure streaming.

## References
- SDK samples in workspace: `../egrabber-win-sample-programs-25.02.0.41/egrabber-sample-programs/cpp/egrabber-snippets`
- Sample: `samples/310-high-frame-rate.cpp` (periodic stats via `StatisticsFrameRate` and `StatisticsDataRate`)
- Configs: `../egrabber-win-sample-programs-25.02.0.41/egrabber-sample-programs/config`

## Local Configuration Script
- Project config script: `egrabberConfig.js` (root of repo)
- Sets ROI to 512x96 and enables triggered acquisition on `LinkTrigger0`.

## Stats Sourcing
- Primary: StreamModule counters `StatisticsFrameRate`, `StatisticsDataRate` (polled ~1 Hz during capture)
- Fallback: compute from part timestamps `BUFFER_INFO_CUSTOM_PART_TIMESTAMPS` and `imageSize`
- Runtime wrapper: `camera/common/EGrabberCamera` exposes the grabber via the shared `ICamera` interface (mirrors sample 310).

## Notes
- For short runs, ensure at least one stats poll occurs before stopping capture.
