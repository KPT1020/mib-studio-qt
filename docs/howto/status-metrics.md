Status metrics in the main window

What’s shown (right of the status bar):
- Display: frames per second actually rendered in the preview (1‑second window)
- Algo: realtime processing FPS (1‑second window)
- Valid/Invalid: classification rates per second (1‑second window)
- Flushed(valid): total valid frames flushed to HDF5 during the active experiment
- Camera: transport stats from the SDK (frame rate, MB/s)

Sources:
- Display FPS: `PlaybackPanel::onLogMetrics()` → `getDisplayFps()`
- Algo/Valid/Invalid FPS: `ProcessingService::realtimeLoop()` → atomics
- Flushed(valid): `ProcessingService::flushBufferedFrames()` after successful append
- Camera stats: `CaptureService::stats()` (EGrabber sample pattern)

Lifecycle:
- On Start Experiment: rates reset to 0; `totalValidFlushed` reset to 0
- On Stop Experiment or Stop Capture: rates reset to 0 (totals unchanged at stop)

Logging:
- Periodic logs from `PlaybackPanel` and `ProcessingService` show the same metrics via spdlog.


