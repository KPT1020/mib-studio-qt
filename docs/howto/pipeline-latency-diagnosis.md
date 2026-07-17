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
| `coalesced` non-zero | several target-group frames merged into one pulse (single-bool request flag) |
| accounting mismatch | unexplained frame loss — investigate |

`grab -> fire (END2END)` in the trigger section is the acquisition→pulse
latency the trigger-timing investigations previously could not observe in
software (see the 2026-04-15 trigger-timing case study in the vault).

## 3. Caveats

- The frame's own `timestamp` (device tick) is **not** on the host clock;
  only `*_us` columns are mutually comparable.
- Async-batch realtime mode records frame identity and callback stamps but
  not per-frame algo start/end (batch algo timing stays aggregate).
- Enabling the recorder costs one atomic load per hook when idle and a few
  clock reads per frame when active — validated by
  `integration.e2e_pipeline_timing`, which also guards the stamp-ordering
  and frame-accounting invariants.
