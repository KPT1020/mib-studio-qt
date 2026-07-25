# ContextBar

> Persistent active-context bar (UX-8, issue #312) under the workflow
> stage bar: Experiment Profile (name + dirty/applied/verified tags),
> camera identity (MOCK clearly labeled), pixel-to-micron calibration,
> operator, storage free space, and system status are visible on every
> workflow stage. Text + glyph, never color alone.

**Source:** `src/frontend/widgets/ContextBar.cpp`,
`include/frontend/widgets/ContextBar.h`
**Related:** [[MainWindow]], [[WorkflowStageBar]],
[[../services/WorkflowStateService]]

## API

- `updateData(Data)` — full re-render from a value snapshot; called from
  `MainWindow::refreshWorkflowState()` (2 Hz + event-driven refreshes).
- `segmentActivated(id)` — segment click; ids `profile`, `camera`,
  `calibration`, `operator`, `storage`, `status`. `MainWindow::
  handleContextSegment` routes them: profile → Profiles surface, camera →
  Connect, calibration → Pixel-to-Micron dialog, operator → name prompt
  (persisted at QSettings `Operator/Name`, recorded in readiness
  provenance), storage → open data folder, status → jump to the
  recommended workflow stage.

## Gotchas

- The operator segment is the only identity surface in the app; the
  readiness gate reuses the same QSettings key for its operator field.
- Status segment styling is red/bold when the recommended stage is
  blocked, amber when any stage needs attention — with text stating the
  same, so color is redundant information.
