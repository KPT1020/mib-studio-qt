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
| R5 | inline | exp | 500 | 0.77 → 0.52 ms (0.67×) | none — full experiment: every-frame accumulation + ~30 MB/s HDF5 flush, 183k frames flushed, 0 dropped |
| R6 | inline | on | 500 | 0.68 → 0.50 ms (0.74×) | none — raw frame recording: 81k frames written alongside realtime |

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
5. **Experiment and recording paths also clean** (R5/R6, via the new
   `--experiment` / `--record` harness flags): sustained flush I/O and the
   frame-recording thread neither accumulate backlog nor drop frames over
   9 minutes. The experiment flush writer does produce occasional multi-ms
   `request_to_fire` scheduling outliers (peak 7.1 ms in R5) on a host
   without RT thread priority — spikes, not a trend.
6. Net: the growth-after-5-minutes symptom did **not** reproduce headlessly,
   which shifts suspicion to the paths the mock harness cannot exercise —
   **H3** (camera/SDK buffering) and **H5** (GUI overlay kernel contention,
   OverviewTab's never-stopped 50 Hz tick) — both discriminated by the
   user-site protocol below, plus the user's actual mode/drop-frames config.

## Fine-grained timing follow-up

Added after the matrix, to close the two measurement blind spots:

- **`fetchStartUs` stamp** in all three inline-loop paths — the slot copy +
  ROI/gray extraction slice (previously invisible inside `grab→algoStart`).
  Measured: ~14 µs p50 / 29 µs p95 on 512x96 ROI frames — small.
- **Empty-frame cost gauge** — empty-classified frames produce no frame
  record but still pay fetch + extraction + blur/threshold/empty-check:
  measured **~80 µs each**. With ~70 % of frames empty in the test stream,
  the realtime thread spends real budget on frames that "don't count" —
  worth knowing when sizing per-image cost (~220–360 µs for full frames).
- **GUI overlay gauge** (`noteOverlayCompute`, always-on, fed by
  `PlaybackPanel::computeProcessedOverlay`) — the analyzer now runs a
  measured A/B within one session: e2e p95 in seconds where the overlay ran
  vs seconds it didn't, printing an H5 verdict (ratio > 1.3×) or an
  explicit "no measurable live-view impact" line. "Live view shouldn't
  impact performance" is now a testable claim on any site recording.

## Profiling layer (beyond timestamps)

Second follow-up: the trend row now profiles *why*, not just *when* —

- Per-stage CPU% + nonvoluntary context switches
  ([[../diagnostics/ThreadRegistry]]: capture / realtime / batch_worker /
  trigger / hdf_writer). First smoke: mock capture thread busy-paces at
  ~100 % CPU (headless artifact — real SDK blocks in grabFrame); realtime
  13 %, hdf_writer 36 % during experiment flush.
- cv::Mat allocation churn ([[../diagnostics/MatAllocStats]] counting
  allocator): **~25k allocs/s ≈ 134 MB/s** at 500 fps on 512x96 frames —
  invisible to RSS, now measured; the churn behind the fragmentation
  hypothesis.
- Allocator heap stats (`Tools::getHeapStats`, glibc mallinfo2): RSS-ramp
  with flat in-use = fragmentation, not leak. Smoke: in-use 310 MB vs RSS
  407 MB.
- HDF5 batch-write cost (`noteHdfWrite` from `HdfWriteQueue::run`): ~311 ms
  avg / 444 ms max per batch during experiment flush, plus cumulative
  process `io_write_mb`.
- Analyzer rules: realtime CPU > 90 % (saturation = leading indicator of
  H2), fragmentation signature, allocation-rate growth, and
  request→fire growth co-occurring with context-switch growth
  (scheduling pressure).

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
