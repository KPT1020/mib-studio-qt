# Long-run frame growth hardening

## Context

User reported random crashes after long runs (>30 minutes) when valid frames
are sparse. The audit focused on unbounded or high-pressure retained state in
the realtime capture/processing/HDF5/UI path.

## Findings

- `FrameStore` and monitoring rings are bounded, but can still be large because
  they retain image payloads.
- Experiment `validFrames_` / `invalidFrames_` were previously unbounded while
  `experimentActive_` was true. A slow or failing HDF5 append could leave the
  async flush in progress while realtime processing continued appending full
  `ProcessedFrame` images.
- `MainWindow::onUpdateStats()` and `StatsDisplayManager` copied full buffered
  `ProcessedFrame` vectors every 500 ms just to read counts.
- `ProcessingService::endExperiment()` read vector sizes without holding
  `framesMutex_`.

## Fix

- Added `ProcessingService::getBufferedFrameCounts()` for count-only status
  polling.
- Added a derived `maxBufferedFrames_` backlog cap based on the flush interval
  (normally 1000-5000 frames, never below the flush interval).
- Routed experiment accumulation through `appendExperimentFrame()`, which drops
  sampled invalid frames first when the cap is reached. Valid frames evict old
  invalid frames before any valid frame is dropped.
- Re-applied the same cap after HDF5 flush failures restore failed batches.
- Locked final experiment count logging in `endExperiment()`.

## Follow-ups

- Consider exposing dropped-frame counters in the sidebar if operators need
  visible feedback when storage cannot keep up.
- The fixed-size `FrameStore` default of 5000 frames can still be a large
  resident footprint for high-resolution cameras.
