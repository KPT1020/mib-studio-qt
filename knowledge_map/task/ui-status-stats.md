Title: Update status bar with real-time metrics

Context:
- Replaced bottom-right “frame” text with actionable metrics.
- Metrics shown: Display FPS, Algo FPS (1s), Valid FPS (1s), Invalid FPS (1s), Total Valid Flushed (experiment).
- Camera transport stats (EGrabber): FPS and MB/s retained.
- Experiment buffer info retained (valid/invalid buffered, flushing state).

Implementation Notes:
- `ProcessingService` publishes 1s-window rates via atomics; resets on experiment start and when capture/experiment stop (rates only). `totalValidFlushed_` resets at experiment start.
- `PlaybackPanel` exposes last computed display FPS (1s window).
- `MainWindow::onUpdateStats()` composes the new status text.

Logging:
- Uses spdlog exclusively.

Follow-ups:
- Consider optional smoothing (5s average) behind a config flag if needed.


