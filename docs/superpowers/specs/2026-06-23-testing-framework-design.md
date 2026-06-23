# MIB Studio Qt — Testing Framework Design

**Status:** Approved design (2026-06-23)
**Scope of this pass:** framework definition + AGENTS.md/strategy-doc updates +
prioritized backlog. The shared support library and the backlog tests are built
incrementally in follow-up work.

## Problem

A cluster of production bugs shipped undetected because the existing tests
(deterministic, single-threaded, happy-path) cannot exercise the failure modes
that actually break the app:

| Bug (this session) | Class not covered before |
|---|---|
| Experiment save fails on a freshly chosen folder/drive | I/O fault paths |
| `TriggerService` lost-wakeup — variable delay / dropped trigger | concurrency (lost wakeup) |
| `TriggerService::stop()` lost-wakeup — **hung capture shutdown** | concurrency (deadlock) |
| Live-view processed-overlay latency grows unbounded | real-time backpressure |
| `FrameStore` returns a stale/aliased frame for evicted indices | concurrency invariant |

The priority capabilities — **run experiments, save data, process images in
real time** — had no systematic safeguard for these classes.

## Goal

A risk-based test taxonomy plus a **capability coverage matrix** that makes the
right tests mandatory for the right changes, backed by sanitizer and soak CI
lanes. Adheres to recognized practice (extended test pyramid, property testing,
ThreadSanitizer/AddressSanitizer, soak testing, regression-test-first); no new
test framework dependency.

## Test categories

Each category maps to a failure mode and has a canonical example already in the
tree.

1. **Unit / behavior** — deterministic, no threads. Logic regressions.
2. **Round-trip** — write → close → reload → verify for every persistence path
   (HDF5 experiment, recording, export, config JSON). Guards silent data loss.
   Example: `recording_lifecycle_test`.
3. **Fault-injection** — drive/path/state failure paths: nonexistent parent
   dir, read-only target, path > Windows `MAX_PATH` (260), invalid name, bad
   input. Guards "saves on one drive but not another."
   Example: `e2e_storage_destinations_test`.
4. **Pipeline e2e** — mock camera → capture → process → store; assert frame
   accounting is conserved (captured == processed + dropped, no silent loss).
   Guards experiments. Example: `kin6_mib_app_capture_proof`.
5. **Concurrency / lifecycle stress** — repeated start/stop/restart and
   record-toggle races, **with a hang watchdog**; run under ThreadSanitizer.
   Guards deadlocks/races/crashes. Example: `e2e_pipeline_stress_test`.
6. **Invariant / property** — ring buffer never returns a torn, stale, or
   evicted-aliased frame; indices monotonic; counts conserved. Example:
   `frame_store_concurrency_test`, `frame_store_bounds_test`.
7. **Performance / latency budget** — steady-state overlay lag bounded, trigger
   jitter bounded with zero missed pulses, throughput floor; gated on
   steady-state/ratios not absolute ms. Guards real-time behavior. Example:
   `e2e_live_view_latency_test`, `e2e_trigger_timing_test`.
8. **Soak (nightly)** — long-duration, high-cycle/high-Hz runs to surface slow
   leaks, accumulation, and rare races. Non-gating.

## Capability coverage matrix (the gate)

A change to a capability must land with its required categories. This is the
rule agents and developers adhere to.

| Capability / area | Required categories |
|---|---|
| **Save data** — `Hdf5Service`, export, recording, config persistence | Round-trip **+** Fault-injection |
| **Run experiments** — capture / processing / recording **lifecycle** | Pipeline e2e **+** Concurrency stress |
| **Real-time processing** — processing, trigger, display, `FrameStore` | Latency budget **+** Invariant |
| **Any code touching threads / shared state** | Passes the **TSan lane** and has/extends a Concurrency stress test |

If a required category does not yet exist for the touched area, creating it is
part of the change.

## Cross-cutting rules (mandatory)

- **No naked `join()`/`wait()` that can hang CI.** Thread/pipeline tests install
  a watchdog that prints the stuck location and `_Exit(99)`s, so a regression
  fails fast with a diagnostic instead of burning the ctest timeout. (`abort()`
  is intercepted by the linked crash handler — use `_Exit`.)
- **Timing tests gate on steady-state or ratios, not absolute milliseconds**, so
  they are machine-independent. Mark probabilistic tests and give them generous,
  stable thresholds.
- **Regression-first.** A bug fix lands with a test proven to fail before the fix
  and pass after (verify-by-revert when feasible).
- **Determinism first.** Prefer deterministic reproduction; only fall back to
  load-induced probabilistic reproduction when the race needs scheduling
  pressure, and document it.
- **Frame accounting is conserved.** Pipeline tests assert no silent frame loss
  (captured == processed + explicitly-dropped).

## Shared test-support library (`tests/support/`, built incrementally)

Removes the copy-paste currently duplicated across tests:

- `assert.h` — `MIB_REQUIRE` / `MIB_EXPECT` with file:line + message.
- `watchdog.h` — RAII `Watchdog`: background thread, `mark(phase, step)`,
  `_Exit(99)` with location if no progress within N seconds.
- `mock_pipeline.h` — fixture that wires `AppBackend` + mock camera + a frames
  source (synthetic or a directory) for e2e/stress tests.
- `tempdir.h` — RAII temp directory.
- `hdf5_roundtrip.h` — open → write → reload → verify helpers + fault-injection
  helpers (make-readonly, oversized path, nonexistent parent).
- `frames.h` — synthetic frame generators (blob, ring) and a real-dir loader.
- `stats.h` — latency/jitter/percentile collectors for budget tests.

## CI lanes (new)

- **Sanitizer lane** (Linux, on PRs): `linux-backend-only` built with
  `-fsanitize=thread`, plus an `-fsanitize=address,undefined` variant, running
  unit / invariant / round-trip / stress tests (excluding network-backed). TSan
  would have flagged both lost-wakeups directly. New CMake preset
  `linux-asan` / `linux-tsan` or a `MIB_SANITIZER` cache option.
- **Nightly soak** (cron): stress / soak / latency at high counts; non-gating,
  uploads logs.
- Existing `backend-ci` (Linux) and Windows lanes unchanged.

## Where it lands

- **AGENTS.md** — concise "Testing framework (safeguards)" section: the coverage
  matrix + the mandatory rules + pointer to the strategy doc.
- **docs/architecture/testing-strategy.md** — full taxonomy, categories, CI
  lanes, support library.
- **This spec** — design rationale + the backlog below.

## Prioritized backlog (implement incrementally)

Ranked by risk-reduction per effort. Items marked ✓ already exist from this
session and only need to be slotted into the framework.

**P0 — close the proven-bug gaps / enable detection**
1. Sanitizer CI lane (TSan first, then ASan+UBSan) — the highest-leverage item;
   catches the entire concurrency/UB class going forward.
2. `tests/support/` watchdog + assert + tempdir (unblocks safe stress tests).
3. Storage fault-injection: extend `e2e_storage_destinations_test` with
   read-only target and disk-full-ish simulation; add export-path coverage. ✓ base
4. Concurrency stress: `e2e_pipeline_stress_test` already guards capture/record
   lifecycle. ✓ — add a camera-reconnect (start→stop→start) variant.

**P1 — broaden core-capability coverage**
5. Round-trip coverage for the **experiment** save path (`saveFrames` /
   `appendFrames` / `writeExperimentInfo` / `writeConfigJson`), not just
   recording mode.
6. Latency-budget test for the **realtime processing** path under ROI + multi
   object (extends `e2e_live_view_latency_test`).
7. Invariant test for `FrameStore::resize()` racing producers/consumers.
8. Frame-accounting assertion added to `kin6_mib_app_capture_proof` (conserve
   captured == processed + dropped).

**P2 — durability**
9. Nightly soak job: long experiment run + continuous start/stop.
10. MockPipeline + hdf5_roundtrip + frames support headers; refactor existing
    tests onto them.
11. Crash-recovery round-trip (`.recovery.h5` fallback path in `loadFile`).

## Out of scope (YAGNI)

- Adopting Catch2/GoogleTest (bespoke harness + support lib is sufficient).
- GUI/Qt-Widgets automated UI testing (manual evidence remains the norm).
- Coverage-percentage targets (capability matrix is the gate, not a %).
