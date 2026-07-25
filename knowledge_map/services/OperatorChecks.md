# OperatorChecks

> Qt-free classification rules for the operator workflow checklists:
> hardware preflight (UX-3, issue #307) and the experiment readiness gate
> (UX-6, issue #310). Facts in, `CheckItem`s out — no side effects.

**Source:** `src/backend/services/OperatorChecks.cpp`,
`include/backend/services/OperatorChecks.h`
**Related:** [[WorkflowStateService]], [[../frontend/ChecklistPanel]],
[[../frontend/MainWindow]]

## Responsibility

- `evaluatePreflight(PreflightFacts)` — camera, processing core, data
  storage (writability + free-space threshold), and Experiment Profile
  checks as `Passed / Warning / Failed / NotRequired`, each with a
  human-readable detail and recovery action. `preflightPassed()` = no
  `Failed` items (warnings allowed).
- `evaluateAlignmentQuality(AlignmentFacts)` — UX-4 signals for the
  Camera & Alignment stage: live-image stream health, ROI, background
  reference, focus ring width (Not required when autofocus is absent;
  Warning when the measurement is missing or stale), calibration.
- `evaluateReadiness(ReadinessFacts)` — the authoritative pre-start
  checklist: preflight facts plus capture running, ROI, operator
  confirmations, background, calibration, profile dirty/applied/verified,
  previous-save health, and double-start protection. `Failed` blocks
  start; `overridable` marks the checks an expert may override (missing
  profile → temporary session, dirty profile, unverified apply, missing
  background/calibration, low disk). Camera stream, core pin, ROI, and
  double-start are **never** overridable.
- `readinessToJson(...)` — serializes the snapshot plus operator name,
  profile, overridden check ids, and override reason for HDF5 provenance.

## Gotchas

- The evaluators are pure: the frontend owns fact collection (storage via
  `QStorageInfo`, profile via `ConfigTabs::profileStatusChanged`) and must
  not re-derive classification rules.
- Mock cameras pass the camera check but the detail text always says
  "training/simulation, no hardware" so screenshots and operators can't
  mistake a mock session for hardware.

**Test:** `backend.operator_checks`
(`tests/backend/operator_checks_test.cpp`).
