# 2026-04-16 — Performance test suite

> Follow-up to [[2026-04-16-thread-performance-audit]]. That audit
> reasoned about latency from code inspection; this task turns the
> reasoning into a runnable perf suite covering every published
> metric. Branch: `claude/add-thread-performance-tests-Xp316`.

## TL;DR — how to run it

```bash
# Build once:
cmake --build build --preset windows-default-build-release --target perf_suite

# Run the full suite, get a summary in build/perf_summary.md:
python scripts/run_perf_suite.py

# Run a subset:
python scripts/run_perf_suite.py --only framestore processing hdf5

# Upload the whole thing to MLflow:
MLFLOW_TRACKING_USERNAME=... MLFLOW_TRACKING_PASSWORD=... \
  python scripts/run_perf_suite.py --upload --tag ci=local
```

**Full how-to:** [`docs/howto/perf-suite.md`](../../docs/howto/perf-suite.md).

The driver runs five CTest binaries, writes each one's JSON alongside,
and then emits `build/perf_summary.md` + `build/perf_summary.json`
aggregating everything. `--upload` delegates to
`scripts/upload_perf_results.py`.

## Motivation

The audit made three concrete claims:

1. `AutofocusService::onRingRatio` is O(1) on the producer thread
   (audit follow-up: sort moved onto `statsThread_`).
2. `TriggerService` wake-up is decoupled from UI-thread work on
   `monitoringFramesMutex_` (audit F1, relies on 2026-04-15 fix).
3. Swapping target-group / ring-ratio callback order doesn't matter now
   that onRingRatio is O(1), but target-group-first stays as a defensive
   invariant (audit F2).

Nothing in the tree measured these. The new test closes that gap.

## What was added

### `src/tests/thread_perf_test.cpp`

Standalone executable wired as a CTest. Drives the two services
directly — **no camera, no Qt event loop, no HDF5**. A tiny
`RecordingTriggerCamera` (anonymous `ICamera` subclass) time-stamps
`setTriggerOutput(true)` transitions so the test can measure wake-up
latency from outside.

Four benchmarks:

| Bench | What it measures | What a regression would mean |
|---|---|---|
| `onRingRatio` (saturated buffer) | Producer-side latency of `AutofocusService::onRingRatio` after pre-filling 1200 samples so `statsThread_` is sorting worst-case. | If p99 > 200 µs, someone likely re-introduced the sort on the producer thread. |
| Trigger wake-up (idle) | Wall-clock from `onTargetGroupResult(true)` to `setTriggerOutput(true)` on a quiet system. | Baseline CV notify + scheduler wake cost. |
| Trigger wake-up (ring-ratio load) | Same, but a producer thread hammers `AutofocusService::onRingRatio` concurrently (forces `statsThread_` to sort continuously). | If this p99 diverges from the idle p99, autofocus stats work is leaking back onto the trigger path. |
| Trigger wake-up (simulated UI snapshot) | A background thread holds a dummy mutex for 1 ms every ~1.5 ms — worst-case model of `monitoringFramesMutex_` hold time. | If trigger latency tracks the UI mutex cadence, the trigger dispatch has accidentally re-acquired a shared mutex. |

Each bench computes min / median / mean / p95 / p99 / max and logs via
spdlog. Iteration counts are tuned for sub-60 s wall clock on a modest
CI runner and are overridable via env vars
(`MIB_THREAD_PERF_RING_RATIO_ITERS`, `MIB_THREAD_PERF_TRIGGER_ITERS`,
`MIB_THREAD_PERF_TRIGGER_LOAD_ITERS`, `MIB_THREAD_PERF_TRIGGER_UI_ITERS`).

### JSON artefact + soft ceilings

The test writes `thread_perf_results.json` (path overridable via
`MIB_THREAD_PERF_JSON`; the CTest sets it to
`${CMAKE_BINARY_DIR}/thread_perf_results.json`). Format:

```json
{
  "on_ring_ratio_latency":           { "n": ..., "min_us": ..., "median_us": ..., ... },
  "trigger_wakeup_idle":             { ... },
  "trigger_wakeup_ring_ratio_load":  { ... },
  "trigger_wakeup_ui_snapshot_sim":  { ... }
}
```

The test also WARN-logs when p99 crosses a soft ceiling (200 µs for
onRingRatio, 5 ms for each trigger bench) but **never fails** — timing
on shared CI runners is noisy, and we want the signal in MLflow, not
a flaky gate.

### `scripts/upload_thread_perf.py`

Reads the JSON, flattens metrics to
`{bench}.{metric}` keys, and logs them to MLflow. Follows the repo
convention (CLAUDE.md): refuses to upload to HTTPS MLflow servers
without `MLFLOW_TRACKING_USERNAME` + `MLFLOW_TRACKING_PASSWORD` in the
environment. Default tracking URI is `https://mlflow.yofo.bio`;
experiment defaults to `thread-performance`. Run name defaults to the
short git SHA (falls back to `GITHUB_SHA` / `BUILD_SOURCEVERSION` /
`local`).

`--dry-run` prints what would be uploaded without touching the network.

## How to run

```bash
# Build (Release, via Conan toolchain on Windows):
cmake --preset windows-default
cmake --build build --preset windows-default-build-release

# Run just the thread perf test:
ctest --test-dir build -C Release -R thread_perf_test -V

# Or invoke the binary directly to tune iteration counts:
MIB_THREAD_PERF_RING_RATIO_ITERS=200000 \
MIB_THREAD_PERF_TRIGGER_ITERS=2000 \
  build/Release/thread_perf_test.exe

# Upload to MLflow (credentials required for HTTPS tracking URIs):
MLFLOW_TRACKING_USERNAME=... MLFLOW_TRACKING_PASSWORD=... \
  python scripts/upload_thread_perf.py \
    --json build/thread_perf_results.json \
    --tag ci=github --tag build=release
```

## Files changed

- `src/tests/perf_common.h` — shared stats + JSON helpers.
- `src/tests/thread_perf_test.cpp` — TriggerService + AutofocusService
  benches (original commit; refactored to use `perf_common.h`).
- `src/tests/framestore_perf_test.cpp` — FrameStore push/get benches.
- `src/tests/processing_perf_test.cpp` — `computeProcessedFrame`
  throughput sweep (3 sizes × {bg, no-bg} × {full, 50% ROI}).
- `src/tests/hdf5_perf_test.cpp` — `appendFrames` / `appendRecordingFrames`
  throughput benches; graceful skip if HDF5 unavailable.
- `src/tests/capture_processing_test.cpp` — extended with JSON emit +
  metric-plumbing invariants (asserts UI-visible atomics are non-zero
  after a 2 s mock-camera run).
- `CMakeLists.txt` — five CTest targets + `ENVIRONMENT` wiring for JSON
  paths.
- `scripts/upload_perf_results.py` (renamed from `upload_thread_perf.py`)
  — accepts repeated `--json`, namespaces metrics by filename stem.
- `knowledge_map/services/TriggerService.md`,
  `knowledge_map/services/AutofocusService.md`,
  `knowledge_map/services/ProcessingService.md`,
  `knowledge_map/services/Hdf5Service.md`,
  `knowledge_map/data-model/FrameStore.md`,
  `knowledge_map/architecture/Threading-Model.md`,
  `knowledge_map/current-state/Recent-Work.md` — each linked to the
  relevant bench.

## Full perf suite

| Test | Binary | JSON | What it measures |
|---|---|---|---|
| Thread audit | `thread_perf_test` | `thread_perf_results.json` | `AutofocusService::onRingRatio` producer latency; `TriggerService` wake-up idle / under ring-ratio load / under simulated UI mutex |
| FrameStore | `framestore_perf_test` | `framestore_perf_results.json` | `pushFrame` / `getLatest` / `getByWriteIndex[ROI]` at 3 sizes; producer-consumer contention |
| Processing algo | `processing_perf_test` | `processing_perf_results.json` | `computeProcessedFrame` at 3 sizes × 2 background modes × 2 ROI modes (12 cells) |
| HDF5 writes | `hdf5_perf_test` | `hdf5_perf_results.json` | `appendFrames` batches 10/100/1000 + sustained + `appendRecordingFrames` |
| End-to-end | `capture_processing_test` | `capture_processing_results.json` | Mock-camera 2 s run; validates all exposed CaptureStats/ProcessingStats atomics |

## Not done (intentionally)

- No CI workflow invocation. Existing `build-windows.yml` still runs
  `ctest` in aggregate; the perf tests get picked up for free but
  MLflow upload is left as a manual step until we decide which runner
  carries the baseline.
- No hardware-in-the-loop end-to-end trigger latency test. That
  remains oscilloscope-measured, per the audit.
- `hdf5_perf_test` does not benchmark `writeImageDataset` vs
  `appendImageDataset` separately — just their composite in
  `appendFrames`. If chunking / growth costs ever matter on their
  own, split the bench then.
- Realtime loop is not exercised end-to-end with synthetic frames —
  `processing_perf_test` drives the pure `computeProcessedFrame` helper
  and the end-to-end test uses a 1-pixel mock frame. A full realtime-
  loop bench with realistic frames is deferred until we see a
  regression these benches miss.
