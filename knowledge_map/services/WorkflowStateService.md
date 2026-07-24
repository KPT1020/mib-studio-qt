# WorkflowStateService

> Authoritative evaluator for the guided four-stage operator workflow
> (UX-1, issue #305): Hardware Preflight → Camera & Alignment →
> Experiment → Review. Derives per-stage status from backend facts plus
> explicit operator confirmation — never from which screen was visited.

**Source:** `src/backend/services/WorkflowStateService.cpp`,
`include/backend/services/WorkflowStateService.h`
**Related:** [[../architecture/AppBackend]],
[[../frontend/WorkflowStageBar]], [[../frontend/MainWindow]]

## Responsibility

- `evaluateWorkflow(facts, preflightConfirmed, alignmentConfirmed)` — pure
  function mapping a `WorkflowFacts` snapshot to a `WorkflowSnapshot`:
  one `WorkflowStageState` per stage (status, accessible status text,
  blocking reasons, recommended action) plus a global recommended
  stage/action.
- `WorkflowStateService` — small thread-safe holder for the operator's
  explicit stage confirmations (`setPreflightConfirmed`,
  `setAlignmentConfirmed`). Owned **by value** inside `AppBackend`
  (`backend.workflow()`), so it is valid before `initialize()` and never
  depends on other services.

## Status model

`NotStarted · NeedsAttention · Ready · Complete · Running` per stage:

- **Preflight** — camera configured + processing core pinned ⇒ `Ready`;
  `Complete` only after explicit operator confirmation. Camera detection
  alone never completes the stage. Failed discovery or a missing pinned
  core ⇒ `NeedsAttention` with recovery text.
- **Camera & Alignment** — gated on Preflight `Complete`; needs capture
  running + valid ROI; `Complete` only on explicit confirmation.
- **Experiment** — `Running` while active (text notes flush-in-progress);
  `Ready` when alignment complete + capture running + core pinned;
  `Complete` after a run whose metadata/provenance save succeeded; a
  failed save ⇒ `NeedsAttention`.
- **Review** — locked (`NeedsAttention`) during an active experiment;
  `Ready` when a saved recording exists; `Complete` while a file is open.

## Gotchas

- Confirmations gate `Complete` but can never override a failed fact: a
  confirmed stage whose prerequisite drops (device loss) falls to
  `NeedsAttention`, and recovers to `Complete` without re-confirmation
  when the fact returns. Tests assert this staleness rule.
- Stage enum values intentionally equal the MainWindow top-level tab
  indices (Connect=0 … Review=3); don't reorder one without the other.
- Confirmations are session-scoped (not persisted): every launch starts at
  the earliest incomplete stage by design.

**Test:** `backend.workflow_state` (`tests/backend/workflow_state_test.cpp`)
— success, warning, failure, device-loss, and recovery paths.
