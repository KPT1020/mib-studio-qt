# 2026-07-29 — Real-time latency growth after ~5 minutes: measurement plan

**Status:** instrumentation landed; headless soak matrix run, verdicts below.
**Report:** a user observes real-time latency increasing after the app has
run for about 5 minutes.
**Branch:** `claude/realtime-latency-investigation-3s0b0u`

## Why measurement first

The existing recorder ([[../diagnostics/PipelineTimingRecorder]]) dumps only
the most recent ~65k frames (~2 min @ 500 fps) at stop — the *tail*, which
cannot show growth over minutes. This task added the missing trend tier and
an analysis decision tree instead of guessing at a fix.

## Ranked hypotheses

- **H1** async-batch queue bufferbloat — `batchQueue_` (cap 4096,
  drop-NEWEST preserves the backlog) fills over minutes on a small rate
  imbalance; standing delay = depth / fps.
- **H2** inline consumer backlog — `dropFrames = rtDropFrames_ &&
  !experimentActive_`: with drops off (or an experiment active)
  `rtLastProcessed_` falls behind until FrameStore eviction → sawtooth +
  `ring_behind`.
- **H3** camera/SDK-side buffering — host grab gap grows, device tick gap
  flat.
- **H4** heap churn/leak — RSS ramp, allocator slowdown
  (time-proportional, persists at lower fps).
- **H5** GUI kernel contention — PlaybackPanel 60 Hz overlay runs the full
  kernel on the GUI thread against `processingKernelMutex_`; OverviewTab's
  50 Hz tick never stops on tab hide. Untestable headless — needs an app
  A/B run (panel visible vs hidden).
- **H6** contour-count growth — algo cost scales with detected objects as
  the scene degrades.

## What landed

- [[../diagnostics/PipelineTrendSampler]] — 1 Hz `pipeline_trend.csv`
  (per-stage p50/p95 via `summarize(4096)`, backlog, batch queue depth,
  skip counters, host/device gaps, objects/frame, live target-latency
  gauges, RSS). Env `MIB_PIPELINE_TREND=1`;
  `AppBackend::setPipelineTrendSampling`.
- `ProcessingService::getRealtimeLastProcessedIndex()` /
  `isExperimentActive()` — atomic reads feeding the backlog gauge.
- `mock_pipeline_timing_run --mode inline|batch` + always-on trend
  sampling — the async-batch path was previously unreachable headlessly.
- `scripts/analyze_pipeline_timing.py` trend section — per-minute windows,
  steady-state ratios (growth = ratio >1.3 + 2σ noise guard), decision-tree
  verdict mapping each growth signature to H1–H6.
- Runbook §1b in `docs/howto/pipeline-latency-diagnosis.md`.

## Headless soak matrix (mock camera, 512x96 frames, 720 s each)

| Run | Mode | Drop | fps | Discriminates |
| --- | --- | --- | --- | --- |
| R1 | inline | on | 500 | baseline / H4 / H6 |
| R2 | inline | off | 500 | H2 |
| R3 | batch | n/a | 500 | H1 |
| R4 | inline | on | 200 | load- vs time-proportional |

### Results

_(to be filled from the soak runs)_

## User-site protocol (covers H3/H5, needs the real app + camera)

1. Launch with `MIB_PIPELINE_TIMING=1 MIB_PIPELINE_TREND=1`, matching the
   user's realtime mode and drop-frames setting (ask — H1 vs H2 hinges on
   it).
2. 12-minute capture with an A/B schedule: minutes 0–4 PlaybackPanel
   visible, 4–8 hidden (also navigate off OverviewTab), 8–12 visible.
   `algo_p95` stepping with visibility → H5.
3. Collect `pipeline_trend.csv`, the stop-time CSVs, and `app.log`; run the
   analyzer.

## Follow-up (after a verdict)

Encode the confirmed failure mode as a regression soak test gating on the
last/first-minute `e2e_frame_p95` steady-state ratio (<1.5) plus frame
accounting, with a watchdog `_Exit(99)`; add to `.github/workflows/soak.yml`
as its first true long-duration entry.
