# WorkflowStageBar

> Horizontal guided-workflow bar rendered above the main tabs (UX-1,
> issue #305). One checkable button per stage with status glyph **and**
> status text (never color alone), blocking reasons in the tooltip, a
> global "Next: …" recommended action, and a contextual confirm button
> for the stages that need explicit operator sign-off.

**Source:** `src/frontend/widgets/WorkflowStageBar.cpp`,
`include/frontend/widgets/WorkflowStageBar.h`
**Related:** [[MainWindow]], [[../services/WorkflowStateService]]

## API

- `updateSnapshot(const WorkflowSnapshot&)` — re-render from the
  authoritative snapshot produced by
  [[../services/WorkflowStateService]]. Display only: the bar never
  decides state.
- `setCurrentStage(int)` — highlight the stage matching the visible tab.
- `setStageNavigationEnabled(int, bool, reason)` — lock navigation to a
  stage (Overview/Review during an active experiment); the reason is
  appended to the tooltip. Call **before** `updateSnapshot()` so tooltips
  include it.
- Signals: `stageSelected(int)` (navigate; stage index == tab index),
  `confirmRequested(int)` (operator confirmed Preflight or Alignment).

## Wiring (in MainWindow)

- A 500 ms `workflowTimer_` calls `MainWindow::refreshWorkflowState()`,
  which collects facts (`isCameraConfigured`, capture running, ROI valid,
  processing-core pin, experiment active/completed/save-ok, review file
  loaded, no-cameras-found) and calls `backend.workflow().evaluate(facts)`.
  State-changing slots (capture/experiment start/stop, connect,
  no-cameras-found) refresh immediately as well.
- `confirmRequested` sets the confirmation on `backend.workflow()` and
  logs it; visiting a tab only calls `setCurrentStage`, never confirms.

## Gotchas

- Status glyphs are built from `QChar` code points, not UTF-8 literals —
  MSVC builds without `/utf-8` would otherwise mojibake them.
- Buttons are `StrongFocus` for keyboard navigation; accessible
  name/description carry the full status + reasons for screen readers.
