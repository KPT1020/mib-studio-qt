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

Each row holds windowed per-stage percentiles (`summarize(4096)`), the
realtime consumer backlog (`latestAvailableIndex − rtLastProcessed`), the
async-batch queue depth, cumulative skip counters, host-vs-device
inter-frame gaps, the always-on target-latency gauges, and process RSS.
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

## 2. Analyze

```
python3 scripts/analyze_pipeline_timing.py <dump_dir>
```

The report prints percentiles per stage plus frame accounting. How to read
it:

| Symptom in report | Meaning |
| --- | --- |
| large `grab -> algo start` | frames queue between capture and the realtime loop — processing can't keep up (check `algo duration`, worker load, drop-frames setting) |
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
