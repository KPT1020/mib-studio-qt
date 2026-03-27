# Diagnostic logging enablement and guide

This build adds throttled instrumentation to investigate performance degradation correlated with increasing valid frames.

## Enabling verbose logs

- Set environment variable `MIB_LOG_LEVEL=debug` before launching to see all diagnostic stats logs. Accepted values: trace, debug, info, warn, err, critical, off.
- At runtime (dev): call `spdlog::set_level(spdlog::level::debug);` early in startup to see DEBUG lines. INFO is enabled by default.
- Optionally set a pattern and sinks per your environment.

## What gets logged

- CaptureService (DEBUG, ~1 Hz):
  - `Capture stats`: fps, MB/s
- ProcessingService (DEBUG, ~1 Hz):
  - `Realtime processing summary`: per-second averages (algo and total ms, fps)
  - `Realtime buffers`: accumulated valid/invalid, monitoring sizes, flush interval, since_last_flush, ROI size, background present, memory MB and peak MB
- ProcessingService (DEBUG, every 500 frames):
  - `Realtime monitoring sizes`: monitoringValid/Invalid sizes and process memory
  - `Accumulated frames`: valid/invalid sizes, flush interval, since_last_flush, process memory
- ProcessingService flush (DEBUG/ERROR, per flush):
  - `HDF5 flush start/end`: counts, duration, memory before/after
  - On failure (ERROR): frames restored + memory
- PlaybackPanel (INFO, ~1 Hz via existing metrics timer):
  - `Playback metrics`: display_fps, avg_latency_ms, total/window drops, overlay_ms, roi_area, image size, overlay size, overlay mode
- MainWindow (INFO, ~1 Hz during experiment):
  - `MainWindow stats`: buffer_fetch_ms (time to copy vectors), valid/invalid/total sizes, playback index range and count, flush interval/in-progress, process memory MB
- FrameStore (DEBUG, every 1000 frames pushed):
  - `FrameStore`: totalWritten, available, capacity

## How to use the data

- Look for unchecked growth in:
  - `acc_valid/acc_invalid` (ProcessingService) between flushes
  - `monitoring` sizes (bounded by 1000 but expensive due to cloned images)
  - `MainWindow buffer_fetch_ms` rising with `total` and correlating with slow UI
- Compare `mem_mb`/`peak_mb` trends against buffer sizes and monitoring sizes.

## Caveats

- Logs are throttled to reduce overhead; bursts still possible under extreme frame rates.
- Memory APIs are Windows-first; non-Windows return 0.


