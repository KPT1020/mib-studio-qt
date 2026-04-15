# ExperimentMonitoringTab

> Live charts while capture is running: histograms (area, deformability,
> brightness) + scatter plots (deformability-vs-area, etc.).

**Source:** `src/frontend/tabs/ExperimentMonitoringTab.cpp`,
`include/frontend/tabs/ExperimentMonitoringTab.h`
**Related:** [[../services/ProcessingService]] (monitoring rings),
[[../frontend/System-Utilities]] (`ZoomableChartView`),
[[Dialogs]] (`MonitoringSettingsDialog`)

## Responsibility

- Read `ProcessingService::getMonitoringValidFrames()` /
  `getMonitoringInvalidFrames()` on a timer (ring buffer of 1000 frames
  each).
- Render via `QtCharts`: `QScatterSeries`, `QHistogramSeries`,
  `QBarSeries`, etc. `frontend::ZoomableChartView` adds scroll/zoom.
- Live totals: valid count, invalid count, algo FPS, valid FPS.
- `showEvent` / `hideEvent` pause rendering when the tab isn't visible.

## Gotchas

- Histograms are computed client-side from the monitoring rings — not
  persisted.
- Chart snapshots to HDF5 use
  [[../services/Hdf5Service]]::`saveChartSnapshot` (used by experiment-save
  to preserve the final view).
- See tasks `knowledge_map/task/ui-status-stats.md` and
  `fps_mbs_zero.md` for common metric-display issues.
