# How to diagnose realtime pipeline / trigger delay

When the live pipeline or the trigger output feels delayed, don't guess —
record per-frame timestamps at every stage and measure where the time goes.
The app ships a purpose-built recorder
([`PipelineTimingRecorder`](../../knowledge_map/diagnostics/PipelineTimingRecorder.md))
that stamps each frame on one host monotonic clock at:

1. **acquisition** — `CaptureService` right after `grabFrame` returns
   (`hostTimestampUs`, carried with the frame through `FrameStore`)
2. **algorithm start / end** — the realtime inline loop
3. **trigger dispatch / callbacks done** — the realtime callback stage
4. **trigger request → wake → fire → pulse done** — `TriggerService`

Records go into lock-free pre-allocated rings, so enabling the
instrumentation cannot itself delay the pipeline or drop frames. Skipped
frames are counted by reason, so `pushed == records + skips` — silent frame
loss shows up as an accounting mismatch instead of disappearing.

## 1. Record a session

Set the environment variable and run the capture session that shows the
delay:

```
MIB_PIPELINE_TIMING=1            # enable at startup
MIB_PIPELINE_TIMING_DIR=<dir>    # optional; default <dataDir>/pipeline_timing
```

Stop capture (or quit). The app dumps three CSVs into the dump directory:

- `pipeline_frames.csv` — one row per frame that reached the realtime
  callback stage (grab / algo start / algo end / dispatch stamps)
- `pipeline_triggers.csv` — one row per trigger pulse (request / wake /
  fire / pulse-done stamps, source `frame_index`, coalesced count)
- `pipeline_skips.csv` — frames the consumer skipped, by reason

The rings retain the most recent ~65k frames / ~16k pulses; for longer
sessions the dump is the tail of the run.

Programmatic control (e.g. from a debug hook):
`AppBackend::setPipelineTimingEnabled(bool)` and
`AppBackend::dumpPipelineTiming(dir)`.

## 1b. Long sessions: latency *growth* needs the trend sampler

At 500 fps the rings above hold only ~2 minutes, so a stop-time dump cannot
answer "does latency grow after N minutes?" — enable the 1 Hz trend time
series as well
([`PipelineTrendSampler`](../../knowledge_map/diagnostics/PipelineTrendSampler.md)):

```
MIB_PIPELINE_TIMING=1            # detailed recorder (feeds the sampler)
MIB_PIPELINE_TREND=1             # 1 Hz pipeline_trend.csv in the same dir
```

Each row holds windowed per-stage percentiles (`summarize(4096)`, including
the `fetch+extract` slice), the realtime consumer backlog
(`latestAvailableIndex − rtLastProcessed`), the async-batch queue depth,
cumulative skip counters, host-vs-device inter-frame gaps, the always-on
target-latency gauges, process RSS, and two cost gauges: the average cost of
empty-classified frames (which never get a frame record — blur/threshold/
empty-check still run before classification, ~80 µs on 512x96 ROI frames)
and the GUI overlay kernel (`overlay_avg_us`/`overlay_count`, fed from
`PlaybackPanel::computeProcessedOverlay`). The overlay counter lets the
analyzer answer "does live view impact pipeline latency" **measurably**: it
compares end-to-end p95 between seconds where the overlay ran and seconds
where it didn't (the site protocol's show/hide schedule produces both) and
prints either an H5 verdict or a "no measurable live-view impact" line.

The trend row also carries a profiling layer beyond timestamps:

- **Per-stage CPU%** (`cpu_capture/realtime/trigger/batch/hdf_writer_pct`)
  via `ThreadRegistry` — saturation headroom per thread; the analyzer warns
  when the realtime thread exceeds 90 % (the leading indicator of backlog).
- **Nonvoluntary context switches** (`cs_nonvol_realtime/trigger`,
  Linux-only) — scheduling pressure; correlated with `request_to_fire`
  growth in the verdict.
- **Allocator-level heap** (`heap_inuse_mb`/`heap_free_mb`, glibc
  mallinfo2) — RSS ramping while in-use stays flat is the fragmentation
  signature, distinguished from a leak.
- **cv::Mat allocation churn** (`mat_allocs`/`mat_alloc_mb`, cumulative)
  via a counting default allocator — direct per-frame churn measurement
  (~25k allocs/s at 500 fps on 512x96 frames).
- **HDF5 write cost** (`hdf_write_avg_us`/`count`/`max_us`) from the
  `HdfWriteQueue` writer thread, plus cumulative process `io_write_mb` —
  correlates flush stalls with latency spikes.
Runtime control: `AppBackend::setPipelineTrendSampling(bool, dir)`. The
analyzer (step 2) detects the file and appends a trend section: per-minute
first/last-window medians, steady-state ratios (growth flagged at >1.3×
plus a 2σ noise guard — ratios, never absolute ms), and a decision-tree
verdict:

| Growth signature | Verdict |
| --- | --- |
| batch mode and `batch_queue_depth` ramps toward its cap | async-batch bufferbloat — standing latency = depth / fps (drop-newest overflow preserves the backlog, so depth, not drops, is the signal) |
| inline and `backlog_frames` ramps (sawtooth + `ring_behind` at the FrameStore window) | consumer backlog — only possible with drop-frames off or an experiment active |
| `frame_age` flat but `algo_p95` ramps with `objects_per_frame_mean` | contour growth (scene/background degradation) |
| `algo_p95` ramps with `mem_mb` at flat object counts | heap growth — confirm time- vs load-proportionality at a lower fps and with an LSan build |
| `host_grab_gap` ramps, `device_tick_gap` flat | acquisition-side (SDK) buffering |
| `request_to_fire_p95` ramps | trigger-thread scheduling degradation |
| nothing grows headless but the app shows the symptom | GUI-side suspects: the 60 Hz overlay kernel on the GUI thread (contends on the kernel mutex), OverviewTab's 50 Hz tick (never stops on tab hide) — A/B the PlaybackPanel visible/hidden in an app run |

## 1c. Site runs: saving the metrics and getting them reviewed by AI agents

A site session's evidence is the dump directory (trend CSV + timing CSVs +
`app.log`). It is written locally and crash-safe (the trend file flushes
every row), but it is only reviewable if it leaves the site machine. After
stopping capture, upload it:

```
pip install mlflow            # once, on the site machine
set MLFLOW_TRACKING_USERNAME=...   # never hardcode; ask ops for credentials
set MLFLOW_TRACKING_PASSWORD=...
python3 scripts/upload_pipeline_diagnostics.py <dump_dir> --tag site=<name> --tag ticket=<id>
```

This creates one MLflow run (experiment `pipeline-latency-diagnostics` on
`mlflow.yofo.bio`, the same server AGENTS.md sends test metrics to) holding
the run's context as params (mode, drop-frames, experiment-active, host),
per-minute medians of the key columns as step metrics (browsable as charts
in the MLflow UI), final steady-state ratios, and **every CSV plus the full
analyzer report as artifacts** — a reviewer needs nothing from the site
machine afterwards.

**AI review loop:** point an agent session (e.g. Claude Code on this repo)
at the run and ask it to review — with `MLFLOW_TRACKING_*` in the agent's
environment it can pull the artifacts over the MLflow REST API, re-run
`scripts/analyze_pipeline_timing.py` locally, and apply the decision tree in
this document. No MLflow access from the agent? Use the fallback: the
uploader's `--offline` mode produces `<dump>_diagnostics.zip`; attach it to
a GitHub issue on the repo (drag & drop) and an agent session can download,
unzip, and analyze it directly.

Why not Sentry for this: Sentry is an error/event store — transactions are
sampled (the playback one is throttled to one per 60 s), there is no
artifact storage for CSV dumps and no queryable per-minute series, so a
latency *trend* cannot round-trip through it. Sentry stays what it is good
at (crashes, breadcrumbs, release health); the measurement evidence goes to
MLflow.

## 2. Analyze

```
python3 scripts/analyze_pipeline_timing.py <dump_dir>
```

The report prints percentiles per stage plus frame accounting. How to read
it:

| Symptom in report | Meaning |
| --- | --- |
| large `grab -> algo start` | frames queue between capture and the realtime loop — processing can't keep up (check `algo duration`, worker load, drop-frames setting) |
| large `fetch+extract` | the ring-slot copy + ROI/gray extraction itself is slow (frame size/memory bandwidth) — this slice sits inside `grab -> algo start` and was previously unstamped |
| large `algo duration` | the algorithm itself is the bottleneck (ROI too big, config too heavy, core regression) |
| large `request -> wake` | trigger thread starved by OS scheduling (CPU load; consider RT thread priority — tech-debt P9) |
| large `wake -> fire` | `setTriggerOutput` (grabber I/O) is slow |
| steady device tick gap but growing host grab gap | frames buffer inside the camera/SDK before `grabFrame` returns — acquisition-side delay, not app-side |
| `dropped_to_latest` high | live view intentionally skipping to newest (drop-frames mode, default ON outside experiments) — overlay lag, not loss |
| `ring_behind` non-zero | consumer fell out of the FrameStore window — genuine overrun |
| `TriggerService::getDroppedRequestCount()` non-zero | target-group frames arrived faster than pulses could fire for longer than the 8-deep request queue could absorb — sustained overload (`coalesced` in the CSV is 0 since the per-request queue landed; it only appears in pre-#283 recordings) |
| accounting mismatch | unexplained frame loss — investigate |

`grab -> fire (END2END)` in the trigger section is the acquisition→pulse
latency the trigger-timing investigations previously could not observe in
software (see the 2026-04-15 trigger-timing case study in the vault).

## 3. Dry-run without hardware (mock camera)

The full pipeline (MockCamera → CaptureService → FrameStore →
ProcessingService → TriggerService) can be exercised headlessly with the
`mock_pipeline_timing_run` harness. MockCamera simulates the trigger output
line, so pulses and their timing records are real.

```bash
# fetch stream frames from the public HF dataset (512x96 grayscale TIFFs)
python3 scripts/fetch_hf_512x96stream.py --out /tmp/frames512x96 --count 1000

cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build --target mock_pipeline_timing_run

# ROI defaults to the RIGHT THIRD of the field of view; background defaults
# to the per-pixel median of sampled frames; target-group gates are wide
# open so every valid detection fires TriggerService.
./build/linux-backend/mock_pipeline_timing_run \
    --frames /tmp/frames512x96 --fps 500 --duration 20 \
    --data-dir /tmp/timing_run --out /tmp/timing_run/pipeline_timing

python3 scripts/analyze_pipeline_timing.py /tmp/timing_run/pipeline_timing
```

Options: `--roi x,y,w,h`, `--background <image>`, `--drop-frames` (default is
every-frame mode), `--fps`, `--duration`, `--mode inline|batch` (async-batch
realtime path), `--experiment` (real experiment lifecycle: HDF5 file +
`startExperiment` forcing every-frame accumulation + ~1 Hz flush + full stop
sequence — heavy disk I/O, roughly 30 MB/s at 500 fps on 512x96 frames), and
`--record` (raw frame recording thread). The harness always writes
`pipeline_trend.csv` (§1b), so a long `--duration` (e.g. 540+) plus the
analyzer's trend section is the standard reproduction for latency-growth
reports.

Reference numbers from 20 s / 500 fps runs of `gavinlouuu/512x96stream`
(1000 frames, Linux container, every-frame mode), zero drops and accounting
conserved in both:

| stage (p50 / p95) | before #282+#283 | after |
| --- | --- | --- |
| grab → algo start | 1.03 / 2.01 ms (2 ms idle-poll bound) | 0.083 / 0.138 ms (event-driven wake) |
| algo duration | 0.30 / 0.44 ms | 0.34 / 0.47 ms |
| trigger request → fire | 0.053 / 0.094 ms | 0.060 / 0.106 ms |
| grab → fire (end-to-end) | 1.44 / 2.41 ms | 0.50 / 0.70 ms |

Trigger accounting before: 5/2176 requests coalesced (single-bool flag);
after: exactly one pulse per target frame, zero coalesced, zero dropped.
Rare multi-ms scheduling outliers remain on both stages (max ≈ 30-40 ms) —
that is the no-RT-thread-priority tech debt (P9 / issue #227), not the
pipeline structure.

## 4. Caveats

- The frame's own `timestamp` (device tick) is **not** on the host clock;
  only `*_us` columns are mutually comparable.
- Async-batch realtime mode records frame identity and callback stamps but
  not per-frame algo start/end (batch algo timing stays aggregate).
- Enabling the recorder costs one atomic load per hook when idle and a few
  clock reads per frame when active — validated by
  `integration.e2e_pipeline_timing`, which also guards the stamp-ordering
  and frame-accounting invariants.
