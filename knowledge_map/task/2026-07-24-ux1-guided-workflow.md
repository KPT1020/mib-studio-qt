# 2026-07-24 — UX-1: guided four-stage operator workflow (issue #305)

**Epic:** #304 (operator-guided workflow redesign). The child issues were
written against the React/Tauri migration; this task implements the
foundation stage — UX-1 — **in the Qt app**, since the epic's principles
(authoritative stage state, no completion-by-navigation) apply to both.

## What shipped

- [[../services/WorkflowStateService]] — Qt-free
  `evaluateWorkflow(facts, preflightConfirmed, alignmentConfirmed)` plus a
  thread-safe confirmation holder owned **by value** in
  [[../architecture/AppBackend]] (`backend.workflow()`).
- [[../frontend/WorkflowStageBar]] — stage bar above the main tabs in
  [[../frontend/MainWindow]]; stage index == tab index (Connect=0,
  Overview=1, Experiment=2, Review=3). Status glyph + text, blocking
  reasons and lock reasons in tooltips, global "Next:" action, contextual
  Confirm Preflight / Confirm Alignment & ROI buttons.
- `MainWindow::refreshWorkflowState()` — 500 ms timer + immediate
  refreshes; `HdfReviewTab::hasLoadedFile()` feeds the Review stage.
- Test `backend.workflow_state` covering success, warning, failure,
  device-loss, and recovery paths, including the staleness rule
  (confirmation never overrides a failed fact).

## Deliberate choices / open ends

- Confirmations are session-scoped; every launch starts at the earliest
  incomplete stage (tab 0). Persisting them would fake completed checks
  on a restarted instrument.
- The existing auto-navigation on camera connect (Connect → Overview) was
  kept for parity; UX-1 only requires that connection not *complete*
  Preflight, which holds. Revisit under UX-3 (#307).
- Preflight facts are currently camera-configured + discovery result +
  processing-core pin. UX-3 (#307) will widen them to per-device checks;
  UX-6 (#310) owns the start-gate. `WorkflowFacts` is the extension
  point.
- The mock E2E acceptance item is covered at the evaluator level; full
  UI-level E2E belongs to UX-11 (#315).
