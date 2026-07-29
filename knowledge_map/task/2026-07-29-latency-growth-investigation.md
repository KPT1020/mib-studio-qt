# 2026-07-29 — Real-time latency growth after ~5 minutes: measurement plan

**Status:** instrumentation landed; headless matrix complete — no growth
reproduced headlessly; awaiting a user-site run (H3/H5).
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

## Headless soak matrix (mock camera, 512x96 frames, 540 s each)

Run on a Linux container (no RT priority, mock camera, 1000-frame HF
`gavinlouuu/512x96stream` loop). 540 s per run (environment caps tracked
background tasks at 10 min) — still 4 min past the reported onset, 9 trend
windows.

| Run | Mode | Drop | fps | e2e_frame p95 first→last | Growth verdict |
| --- | --- | --- | --- | --- | --- |
| R1 | inline | on | 500 | 1.08 → 0.74 ms (0.68×) | none |
| R2 | inline | off | 500 | 0.89 → 0.76 ms (0.86×) | none — backlog peak 17 frames, 0 `ring_behind` |
| R3 | batch | n/a | 500 | 13.8 → 13.9 ms (1.01×) | none — queue depth ~3, peak 8 |
| R4 | inline | on | 200 | 1.22 → 0.74 ms (0.61×) | none |

### Findings

1. **The headless pipeline core does not degrade over 9 minutes** in any
   mode on this container: H1 (queue depth flat), H2 (no backlog even in
   every-frame mode — this CPU outpaces 500 fps), H6 (objects/frame flat)
   all exonerated *for this environment*. H2 remains possible on user
   hardware where `algoAvgUs` exceeds the frame interval — the trend file
   from a site run decides it.
2. **H4 (heap growth) rejected**: RSS creep is small and *load-proportional*
   (~15 MB over 9 min @500 fps, ~7.6 MB @200 fps), and most of each run's
   final jump coincides with the stop-time ring dump (which allocates
   ~65k-record copies). No time-proportional leak signature.
3. **Async-batch mode carries ~19× the standing latency of inline**
   (13.8 ms vs 0.74 ms e2e p95; batch-of-16 aggregation + 10 ms max batch
   delay) — constant, not growing, but if the reporting user's config is in
   async-batch mode this alone explains "high latency" perception.
4. **Trigger-thread jitter**: R2 flagged `request_to_fire_p95` 1.36×
   (127 → 173 µs, peak 1 ms) — OS scheduling on a container without
   SCHED_FIFO; absolute magnitude too small to move end-to-end latency.
5. Net: the growth-after-5-minutes symptom did **not** reproduce headlessly,
   which shifts suspicion to the paths the mock harness cannot exercise —
   **H3** (camera/SDK buffering) and **H5** (GUI overlay kernel contention,
   OverviewTab's never-stopped 50 Hz tick) — both discriminated by the
   user-site protocol below, plus the user's actual mode/drop-frames config.

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
