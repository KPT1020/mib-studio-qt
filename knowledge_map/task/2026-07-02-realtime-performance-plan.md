# 2026-07-02 — Real-time performance examination + remediation plan

> Examination of every component on the real-time hot path (capture →
> processing → trigger/display → HDF5) and a committed execution plan to
> resolve the remaining performance issues. Branch:
> `claude/realtime-performance-plan-9k8es2`. Plan:
> `docs/exec-plans/active/2026-07-02-realtime-performance.md`.

## What was examined

Three sweeps: the vault/docs architecture notes, a line-level source audit of
the hot paths (`CaptureService`, [[../data-model/FrameStore]],
[[../services/ProcessingService]], [[../services/TriggerService]],
`AppBackend` recording thread, `OverviewTab` display tick), and the known-debt
/ test landscape (`tests/performance/pipeline_timing_benchmark.cpp`, the e2e
latency/throughput/trigger tests).

## Confirmed already fixed (no re-planning)

- FrameStore two-tier locking (2026-06-17), see [[../data-model/FrameStore]].
- `HdfWriteQueue` write decoupling + `FrameStore::reserveFrameBytes` — the
  2026-06-24 `docs/superpowers/plans/2026-06-24-highspeed-capture-buffering.md`
  plan is **fully implemented**; it was annotated `Status: completed` as part
  of this task (it previously had no status line, cf. TD-2 pattern).
- Trigger-callback hoisting/ordering ([[2026-04-15-trigger-timing-bug]],
  [[2026-04-16-thread-performance-audit]]).
- Autofocus sort off the realtime thread; shared_ptr contours; bbox brightness
  scan; HDF5 interval flush; bounded experiment backlog + count-only polling.

## Remaining issues found (P1–P9)

| ID | Summary | Where |
|----|---------|-------|
| P1 | Recording thread: per-frame config copy, ROI lock, full-frame background `clone()`, and a full-frame gray copy + 2× blur inside `isFrameEmpty()` | `AppBackend.cpp:933-937`, `ProcessingService.cpp:358-363,518-560` |
| P2 | Monitoring rings: 2 full-frame clones **per detected object** per frame, even with no consumer | `ProcessingService.cpp:1645-1667,2150-2152` |
| P3 | Experiment backlog trim: `vector::erase(begin())` under `framesMutex_` → O(n²) under backpressure | `ProcessingService.cpp:996-1002,1052-1053` |
| P4 | Experiment full-frame path: ~4-5 full-frame allocations/copies per saved frame | `ProcessingService.cpp` ~2064-2536 |
| P5 | Snapshot publication: full-frame `mask.clone()` inside `snapshotMutex_` every frame | `ProcessingService.cpp:1680,2284-2314` |
| P6 | Display tick: two full-frame copies per 20 ms + smooth rescale on every paintEvent | `OverviewTab.cpp:184-207`, `SimpleImageCanvas.cpp:79` |
| P7 | Realtime loop: whole-config copy per frame; callback `std::function` copies per object | `ProcessingService.cpp:1968-1976,1625-1641` |
| P8 | `FrameStore` frame-filter is dead code (zero callers); vault notes were stale | `FrameStore.cpp:41-62,99-114` |
| P9 | No RT thread priority for trigger/realtime threads → tech-debt TD-7 | [[../services/TriggerService]] note |

## Outcome

- New execution plan `docs/exec-plans/active/2026-07-02-realtime-performance.md`
  (Status: active) with a 6-PR breakdown: PR1 measurement-first benchmark parts
  (C)/(D)/(E) + housekeeping, PR2 recording hot path (P1+P8), PR3 experiment
  path (P3+P4), PR4 realtime loop (P2+P5+P7), PR5 display (P6), PR6 = TD-7
  disposition for P9.
- `docs/exec-plans/tech-debt-tracker.md` gained TD-7 (RT thread priority).
- No C++ changes in this task — code lands via the plan's PRs, each with the
  coverage-matrix tests (latency budget + invariant; TSan + stress for
  shared-state changes) and matching vault updates.
