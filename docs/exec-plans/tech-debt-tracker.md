# Tech Debt Tracker

Known debt and deviations from [`../golden-principles.md`](../golden-principles.md).
Each entry needs an exit criterion so an agent can pick it up and know when it
is done. Remove entries in the same PR that resolves them.

| ID | Area | Debt | Exit criterion |
|----|------|------|----------------|
| TD-1 | tooling | `.clang-format` exists but the existing codebase has never been bulk-formatted; CI does not gate on format | Decide: bulk-format in one commit + add CI gate, or keep format-on-touch policy |
| TD-2 | docs | `docs/superpowers/plans/` predates `docs/exec-plans/`; the two 2026-04-16 plans were never marked completed | Verify both plans shipped, annotate status, and stop adding plans to `superpowers/` |
| TD-3 | docs | Vendor material (`docs/Longer Pump dLSP501 Modbus RTU Series/`, `docs/mib_grabber.cpp`) sits loose in `docs/` | Move under `docs/integration/` or a `docs/vendor/` folder and link from the index |
| TD-4 | testing | No CI coverage for Windows build of the full app (only packaging-script validation in `ci.yml`; full builds are manual `workflow_dispatch`) | Scheduled or PR-triggered Windows build job, or documented decision not to |
| TD-5 | tooling | No pre-commit hook; doc/vault checks rely on agents remembering `scripts/check_docs.py` | Add pre-commit config running `check_docs.py` (and clang-format on staged C++) |
| TD-7 | realtime | Trigger and realtime-inline threads run at default OS priority; trigger-onset jitter under load is tens-to-hundreds of µs (documented `TriggerService` limitation; the `e2e_trigger_timing_test` ≤50 ms gate is currently met). Related shutdown-only latency: `EGrabberCamera::stop()` sleeps ~360 ms under `stateMutex_`. See `docs/exec-plans/active/2026-07-02-realtime-performance.md` (P9) | Opt-in (default off) priority raise for the trigger + realtime threads — Windows `SetThreadPriority`, best-effort `pthread_setschedparam` — validated by `e2e_trigger_timing_test` on hardware, with documented rollback; or a documented decision not to |
