# RunDashboardStrip

> Run header + key-metrics strip of the integrated Experiment dashboard
> (UX-7, issue #311). Installed above the live image on
> [[PreviewPage]] so routine operation needs no Preview/Monitoring
> sub-tab switching: run state chip, elapsed time, camera/valid/invalid
> rates, saved/buffered totals, metric staleness, and persistent alerts.

**Source:** `src/frontend/widgets/RunDashboardStrip.cpp`,
`include/frontend/widgets/RunDashboardStrip.h`
**Related:** [[PreviewPage]] (`setDashboardWidget`), [[MainWindow]],
[[ExperimentMonitoringTab]] (detailed charts remain there)

## Behavior

- State chip: IDLE / LIVE (not recording) / RUNNING / FLUSHING /
  SAVE FAILED — text on colored background, accessible name included.
- Metrics line appends `[metrics stale]` when the processing metric age
  exceeds 3 s while capturing — stale numbers never masquerade as live.
- Alert row (red, persistent until the condition clears): save error,
  camera stopped during an active experiment, unwritable data folder,
  low disk (< 5 GB).
- Fed from `MainWindow::refreshWorkflowState()` every 500 ms using
  count-only/atomic backend getters (no frame copies on the UI timer).

## Gotchas

- Detailed charts and trigger controls intentionally stay in
  [[ExperimentMonitoringTab]]; this strip is the always-visible subset
  the operator needs to run the instrument (UX-9 moves the trigger
  controls behind commissioning mode).
