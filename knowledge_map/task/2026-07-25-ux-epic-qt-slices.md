# 2026-07-25 — UX epic #304 slices UX-2…UX-11 for the Qt app

Continues [[2026-07-24-ux1-guided-workflow]]. All remaining epic slices
implemented in the Qt app, one commit per slice group, each validated
headless through the extended screenshot tour.

## What shipped per slice

- **UX-2 (#306)** — template labeled `Template: built-in defaults
  (unvalidated)`; `ConfigTabs::profileStatusChanged` feeds cached profile
  facts into workflow state; Experiment stage cannot be Ready without an
  explicit compatible profile.
- **UX-3 (#307)** — [[../services/OperatorChecks]] `evaluatePreflight` +
  [[../frontend/ChecklistPanel]] on the Connect tab with `Fix…` recovery
  routing; storage probed via `QStorageInfo`.
- **UX-4 (#308)** — `evaluateAlignmentQuality` panel on Overview (stream,
  ROI, background, focus ring width + staleness, calibration).
- **UX-5 (#309)** — `ConfigTabs::onApplyAndVerifyProfile` transaction
  with per-component results and invalidation (selection/config/
  reconnect). Camera-script readback is reported honestly as "Applied,
  not externally verified".
- **UX-6 (#310)** — `ReadinessDialog` gate at Start; non-overridable
  blocks vs. explicit overrides (operator + reason); provenance written
  as `readiness_json` on `/experiment_info`.
- **UX-7 (#311)** — [[../frontend/RunDashboardStrip]] on the Preview page
  (state chip, elapsed, rates, staleness marks, persistent alerts).
- **UX-8 (#312)** — [[../frontend/ContextBar]] under the stage bar with
  click-through segments; operator identity at QSettings `Operator/Name`.
- **UX-9 (#313)** — Service/Commissioning mode: confirmation to enter,
  amber banner, trigger controls hidden in Operator mode, confirm-before-
  fire pulses, forced Operator on experiment start, session-scoped.
- **UX-10 (#314)** — Review Run context panel from
  `readConfigJson`/`readReadinessJson`/`readProcessingCoreIdentity`;
  legacy fields display "not recorded".
- **UX-11 (#315, partial)** — three new deterministic screenshot states
  wired into the manual; evaluator-level tests for every classification
  rule (`backend.workflow_state`, `backend.operator_checks`,
  `backend.hdf_provenance_roundtrip`).

## Open ends (honest scope notes)

- UX-5 has no true hardware readback (EGrabber apply offers none here);
  a future readback contract can upgrade "Applied" → "Verified".
- UX-7 keeps detailed charts on the Monitoring sub-tab; the strip covers
  the routine-operation acceptance, not full chart consolidation.
- UX-10 comparison-between-runs and create-revision-from-run are not
  implemented; the summary/provenance half is.
- UX-11 usability sessions with real operators and the recorded
  supported-hardware acceptance run remain outstanding (need humans and
  a Windows instrument).
- The stray empty `private:` left in `ExperimentMonitoringTab.h` when the
  commissioning method was inserted is harmless but can be tidied.
