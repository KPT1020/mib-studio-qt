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
- `showEvent` / `hideEvent` pause rendering when the tab isn't visible **and**
  gate the backend accumulation: `showEvent` calls
  `ProcessingService::setMonitoringActive(true)`, `hideEvent` calls
  `setMonitoringActive(false)`. Monitoring rings only fill while this tab is
  visible — when hidden, the realtime loop skips the per-object frame copies
  entirely (see [[../services/ProcessingService]] "Monitoring rings").
- **Top-row trigger controls:**
  - `sortTriggerBtn` — single manual pulse; calls
    `backend_.trigger().onTargetGroupResult(services::TargetGroupSignal{.isTargetGroup=true})`.
  - `triggerDurationSpin` — pulse width in µs
    (`TriggerService::setPulseDurationUs`).
  - `periodicTriggerBtn` (checkable) + `periodicTriggerIntervalSpin` —
    periodic test pulses. When armed, `periodicTriggerTimer_`
    (`QTimer`) fires `onTargetGroupResult(services::TargetGroupSignal{.isTargetGroup=true})`
    every N ms. The
    interval spinbox is disabled while armed; the timer is disarmed in
    `hideEvent`. Intended for oscilloscope/sorter bring-up without
    needing live target-group classifications. See
    [[../services/TriggerService]].

## Gotchas

- Histograms are computed client-side from the monitoring rings — not
  persisted.
- `loadCurrentConfig()` now refreshes the histogram ring-ratio defaults as
  well as the Tune Params widgets, so config reloads keep the visible chart
  range aligned with the saved thresholds.
- Chart snapshots to HDF5 use
  [[../services/Hdf5Service]]::`saveChartSnapshot` (used by experiment-save
  to preserve the final view).
- See tasks `knowledge_map/task/ui-status-stats.md` and
  `fps_mbs_zero.md` for common metric-display issues.
