# Performance Test Suite

The perf suite is **five CTest binaries behind one Python driver**. You
build once, then run the driver as a tool.

```
┌───────────────────────────────────────────────────────────────┐
│  scripts/run_perf_suite.py                                    │
│     └─ runs ─┬─ thread_perf_test      → thread_perf_results.json
│              ├─ framestore_perf_test  → framestore_perf_results.json
│              ├─ processing_perf_test  → processing_perf_results.json
│              ├─ hdf5_perf_test        → hdf5_perf_results.json
│              └─ capture_processing_test → capture_processing_results.json
│     └─ aggregates → build/perf_summary.md + build/perf_summary.json
│     └─ (optional) --upload → MLflow
└───────────────────────────────────────────────────────────────┘
```

## TL;DR — one command

```bash
# Build once
cmake --preset windows-default
cmake --build build --preset windows-default-build-release --target perf_suite

# Run the suite, see the summary, done
python scripts/run_perf_suite.py
```

That's it. You get:

- **`build/perf_summary.md`** — human-readable table of every metric.
- **`build/perf_summary.json`** — same data, machine-readable.
- **Per-test JSON reports** next to them, if you want to look at one.

## What each binary measures

| Binary | Covers | Key metrics |
|---|---|---|
| `thread_perf_test` | `TriggerService` + `AutofocusService` | `onRingRatio` producer p99, trigger wake-up p99 (idle, under ring-ratio load, under UI-mutex sim) |
| `framestore_perf_test` | `FrameStore` (producer/consumer ring) | `pushFrame` / `getLatest` / `getByWriteIndex[ROI]` latency at 512/1024/2048 px; producer-consumer throughput |
| `processing_perf_test` | `ProcessingService::computeProcessedFrame` | µs/frame and implied FPS for 3 sizes × {bg, no-bg} × {full, 50% ROI} (12 cells). Ground truth for `getAlgoAvgUs1s()`. |
| `hdf5_perf_test` | `Hdf5Service::appendFrames` + recording | frames/s, MB/s for batch sizes 10/100/1000; sustained 10×100; `appendRecordingFrames` separately. Skips if HDF5 missing. |
| `capture_processing_test` | End-to-end mock-camera → processing | Validates every exposed atomic (`framesProcessed`, `lastFrameRate`, `lastDataRateMBps`, `jobsQueued/Processed`, `algoFps1s`, `validFps1s`, `invalidFps1s`, `algoAvgUs1s`, `totalValidFlushed`). Sanity-fails if any plumbing invariant is broken. |

## Common workflows

### Run a subset (e.g. only the portable tests after a FrameStore change)

```bash
python scripts/run_perf_suite.py --only framestore processing hdf5
```

### Re-aggregate existing results without re-running

Useful when you ran one binary by hand to iterate and want the full
summary regenerated:

```bash
python scripts/run_perf_suite.py --no-run
```

### Stream live output (noisy but useful when a test hangs)

```bash
python scripts/run_perf_suite.py -v
```

### Upload to MLflow after running

```bash
export MLFLOW_TRACKING_USERNAME=...
export MLFLOW_TRACKING_PASSWORD=...
python scripts/run_perf_suite.py --upload --tag ci=local --tag build=release
```

Experiment defaults to `mib-studio-perf`; run name defaults to the
short git SHA. Override with `--experiment NAME` / `--run-name NAME`.

### Just upload previously-generated JSONs

```bash
python scripts/upload_perf_results.py \
    --json build/thread_perf_results.json \
    --json build/framestore_perf_results.json \
    --json build/processing_perf_results.json \
    --json build/hdf5_perf_results.json \
    --json build/capture_processing_results.json
```

### Run from CTest directly (no driver)

Every perf test is labelled `perf`, so:

```bash
ctest --test-dir build -C Release -L perf --output-on-failure
```

This runs them but does **not** aggregate results — use the driver for
that.

## Tuning iteration counts

Each binary honours env vars to scale up for local long-running
sweeps (defaults target under ~60 s total per binary):

| Binary | Env var | Default |
|---|---|---|
| `thread_perf_test` | `MIB_THREAD_PERF_RING_RATIO_ITERS` | 50000 |
| | `MIB_THREAD_PERF_TRIGGER_ITERS` | 500 |
| | `MIB_THREAD_PERF_TRIGGER_LOAD_ITERS` | 500 |
| | `MIB_THREAD_PERF_TRIGGER_UI_ITERS` | 500 |
| `framestore_perf_test` | `MIB_FRAMESTORE_PUSH_ITERS` | 2000 |
| | `MIB_FRAMESTORE_GET_ITERS` | 20000 |
| | `MIB_FRAMESTORE_CONTENTION_MS` | 1000 |
| `processing_perf_test` | `MIB_PROCESSING_ITERS` | 200 |
| `hdf5_perf_test` | `MIB_HDF5_BATCHES` | 5 |

Example — crank up the thread test for a nightly run:

```bash
MIB_THREAD_PERF_RING_RATIO_ITERS=500000 \
MIB_THREAD_PERF_TRIGGER_ITERS=5000 \
  build/Release/thread_perf_test.exe
```

## Soft vs hard failures

- `capture_processing_test` is **hard-fail**: it returns non-zero if
  any UI-visible metric atomic is zero after a 2 s mock-camera run
  (broken plumbing is a real bug).
- Every other perf test is **soft-fail**: they WARN-log when p99
  exceeds a generous soft ceiling but always return zero. CI runners
  are too noisy for hard gates; the real signal is the MLflow trend.

## Related

- Task record: `knowledge_map/task/2026-04-16-thread-perf-tests.md`
- Thread audit that motivated the first bench:
  `knowledge_map/task/2026-04-16-thread-performance-audit.md`
- Shared test helpers: `src/tests/perf_common.h`
- Uploader: `scripts/upload_perf_results.py`
