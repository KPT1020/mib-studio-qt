# PipelineTrendSampler

> Periodic (1 Hz) time-series consumer of [[PipelineTimingRecorder]] for
> long-session latency investigations. Appends one CSV row per second —
> windowed per-stage percentiles, queue depths, consumer backlog, skip
> counters, inter-frame gaps on both clocks, and process RSS — so latency
> *growth over minutes* is measurable even though the recorder's rings hold
> only ~2 minutes of frames at 500 fps.

**Source:** `src/backend/diagnostics/PipelineTrendSampler.cpp`,
`include/backend/diagnostics/PipelineTrendSampler.h` (part of
`mib_processing`, Qt-free)
**Related:** [[PipelineTimingRecorder]], [[../services/ProcessingService]],
[[../architecture/AppBackend]], [[../data-model/FrameStore]]
**How-to:** `docs/howto/pipeline-latency-diagnosis.md` (§ "Long sessions")
**Origin:** [[../task/2026-07-29-latency-growth-investigation]]

## Why

The recorder's stop-time CSV dump is the *tail* of the run
(`kFrameCapacity` = 64k frames ≈ 2 min @ 500 fps) — exactly the wrong data
for "latency increases after ~5 minutes" reports. The sampler turns the
recorder's `summarize(sampleLimit)` plus a set of always-available gauges
into an append-only trend file (`pipeline_trend.csv`) that
`scripts/analyze_pipeline_timing.py` reduces to per-minute steady-state
ratios and a decision-tree verdict (bufferbloat vs consumer backlog vs
SDK-side queueing vs heap growth vs contour growth).

## Design

- Own normal-priority thread; condition-variable wait with a 1 s tick, so
  `stop()` joins within one interval (no naked unbounded join).
- Strictly a *reader*: `summarize(4096)` + `frameRecords()` tail +
  relaxed-atomic counters — the recorder's designed concurrent-read mode.
  Nothing on any pipeline hot path; the allocate-and-sort cost lives on the
  sampler thread.
- Application gauges arrive via an injected
  `Provider` (`std::function<PipelineTrendProviderSample()>`) so the
  diagnostics layer stays decoupled: `AppBackend` supplies FrameStore write
  head, `ProcessingService::getRealtimeLastProcessedIndex()` (→ backlog),
  capture/algo fps, `getBatchPipelineStats()`, mode/drop-frames/experiment
  flags.
- Gap statistics are computed per tick over only the records new since the
  previous tick, host grab stamps and device ticks reduced **separately**
  (different clocks — never subtracted from each other).
- Rows are flushed as written (a crash loses ≤1 s of trend data).

## Enabling and output

- Env: `MIB_PIPELINE_TREND=1`, read in `AppBackend::initialize`; rows go to
  `pipeline_trend.csv` in the same directory as the timing CSVs
  (`MIB_PIPELINE_TIMING_DIR` / `<dataDir>/pipeline_timing`).
- Runtime: `AppBackend::setPipelineTrendSampling(bool, dir)`.
- `tests/tools/mock_pipeline_timing_run` always enables it (long `--duration`
  soaks are its reason to exist) and grew a `--mode inline|batch` flag for
  exercising the async-batch queue path.
- Analyse: `python3 scripts/analyze_pipeline_timing.py <dir>` — the trend
  section prints per-minute first/last window medians, steady-state ratios
  (growth flagged at ratio > 1.3 plus a 2σ noise guard), and the verdict.

## Gotchas

- The percentile columns are a *rolling* ~4096-record window, not a
  per-second window; the gap/object columns are per-tick windows.
- In async-batch mode `frame_age`/`algo` columns are empty (the recorder
  stores zero `algoStartUs`/`algoEndUs` there) — the H1 verdict rests on
  `batch_queue_depth` and end-to-end columns instead.
- Provider reads live services: `AppBackend::shutdown()` stops the sampler
  first, before any service teardown.
- Sampler columns are meaningful only while the detailed recorder is
  enabled (`MIB_PIPELINE_TIMING=1`); without it only the provider gauges,
  live target-latency EWMA, and RSS columns are populated.
