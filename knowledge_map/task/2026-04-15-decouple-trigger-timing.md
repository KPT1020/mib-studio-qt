# 2026-04-15 — Decouple Trigger Onset From Processing Latency

## Problem

[[../services/TriggerService]] fired its DO pulse on the trailing edge of
[[../services/ProcessingService]]'s realtime loop (at
`targetGroupCallback_(isTargetGroup)`). That callback lives at the bottom
of a variable-duration pipeline (blur → morphology → contour detection →
classification, 1–10 ms), so trigger-onset-from-capture latency tracked
processing-time jitter 1:1. Downstream hardware that assumes a stable
capture-to-pulse interval was suffering.

## Approach

Schedule the pulse at `captureObserved + triggerDelayUs` rather than
firing immediately on classification.

1. Record `steady_clock::now()` in [[../services/CaptureService]] right
   after `camera->grabFrame()` returns. This is the closest CPU-clock
   reference to actual exposure we can cheaply obtain.
2. Propagate it to [[../data-model/FrameStore]] via a new optional
   parameter on `pushFrame`. The store keeps a parallel
   `std::vector<uint64_t> captureSteadyNs_` ring (same capacity/indexing
   as `ring_`), exposed via `getCaptureSteadyNs(idx, outNs)`.
   Out-of-band so `struct Frame` didn't have to change (would have
   rippled into HDF5 writes, TIFF saves, playback copies).
3. [[../services/ProcessingService]]'s realtime loop fetches the
   timestamp per iteration and passes it through the callback:
   `TargetGroupCallback(bool, std::chrono::steady_clock::time_point)`.
   Falls back to `now()` if the sideband has no entry (e.g. after
   `resize()` clears the ring).
4. [[../services/TriggerService]] replaces its single-shot
   `std::atomic<bool> triggerRequested_` with a bounded deque of
   `{captureObserved, deadline}` pairs (`kMaxPendingTriggers = 32`). The
   trigger thread uses `cv.wait_until(front.deadline)` to sleep until
   the scheduled onset. Slip (now > deadline) is logged and recorded
   in `lastSlipUs_`; queue overflow drops the oldest and bumps
   `droppedTriggers_`.
5. New UI control `triggerDelaySpin` in
   [[../frontend/ExperimentMonitoringTab]] wires to
   `TriggerService::setTriggerDelayUs`. Default 0 µs preserves legacy
   fire-ASAP behavior.
6. Manual test button (`onSortTrigger`) now passes `steady_clock::now()`
   as the capture time, so it honours the same delay as real hits.

## Files touched

- `include/backend/playback/FrameStore.h`,
  `src/backend/playback/FrameStore.cpp` — sideband vector + getter.
- `src/backend/services/CaptureService.cpp` — record steady_clock at grab.
- `include/backend/services/TriggerService.h`,
  `src/backend/services/TriggerService.cpp` — deadline queue + delay knob
  + slip/realized metrics.
- `include/backend/services/ProcessingService.h`,
  `src/backend/services/ProcessingService.cpp` — callback signature
  change (3 invocation sites in the realtime loop).
- `src/backend/AppBackend.cpp` — lambda wiring.
- `resources/ui/ExperimentMonitoringTab.ui`,
  `src/frontend/tabs/ExperimentMonitoringTab.cpp` — `triggerDelaySpin`
  control + `onSortTrigger` capture-time propagation.

## Verification

- Build Debug + Release locally (Windows/VS2022, Conan toolchain).
- Run `mock_studio_qt.exe` with `MIB_CAMERA_MODE=mock`, set
  `triggerDelaySpin = 50000` (50 ms), manually click `Sort Trigger`
  repeatedly and watch `TriggerService::getLastRealizedDelayUs()` in
  the logs converge to ≈ 50 000 µs.
- Stress-test: increase `morph_kernel_size` or add a fake 5 ms sleep
  inside the realtime loop (temporarily, for test) and confirm
  `lastRealizedDelayUs` stays flat while the legacy path's onset would
  have bloated.
- Slip path: set delay to 100 µs (below typical processing cost), expect
  `SPDLOG_WARN("TriggerService: slip ...")` lines and non-zero
  `getLastSlipUs()`.
- Queue overflow: delay = 500 ms + burst of target-group hits, expect
  `getDroppedTriggers()` to climb once `kMaxPendingTriggers = 32` is
  reached.
- Regression: delay = 0 gives behavior equivalent to the previous
  `triggerRequested_` signalling (onset immediately after wake).

## Notes

- Hardware (EGrabber) has no scheduled DO pulse; scheduling is in
  software. Slip floor is OS scheduler granularity (~tens of µs on
  Windows). Busy-wait is used only for the pulse width itself, not for
  the scheduled delay.

## Addendum — Hardware-clock anchoring (same day)

**Refinement:** anchor the trigger schedule on the camera's hardware
timestamp (periodic, jitter-free) rather than raw `steady_clock::now()`
at `grabFrame()` return, because the latter is jittered by CPU
scheduling which is itself driven by processing-pipeline load.
[[../services/CaptureService]] now maintains an EMA-smoothed offset
between the two clocks (`cpu_ns - hw_ns`, weight `1/64`) and pushes the
**predicted** CPU-clock capture time (`frame.timestamp` + smoothed
offset) into FrameStore's sideband. Downstream (ProcessingService,
TriggerService) is unchanged — it already consumes this timestamp as the
scheduling anchor, so onsets now land on the hardware's periodic grid.

Makes the per-frame delay (from raw CPU observation) effectively
variable: `triggerDelayUs + (predicted − observed)`, which is precisely
the adjustment needed to land on a unified onset time.

Bootstrap / reset handling:
- First frame → smoothed offset = raw offset, no smoothing.
- Raw offset jump > 100 ms (e.g. camera re-arm) → re-bootstrap with WARN.
- `frame.timestamp == 0` (some mock paths) → fall back to raw
  `steady_clock::now()`.

New `CaptureStats` members: `clockOffsetNs` (smoothed) and
`lastRawOffsetNs` (latest raw sample) for diagnostics.
