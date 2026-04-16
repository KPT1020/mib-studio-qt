# 2026-04-16 — Thread performance audit (focus: trigger latency)

> Audit of every long-running thread with attention to coupling between
> the realtime processing pipeline and the trigger service. Branch:
> `claude/audit-thread-performance-Pr9OI`.

## Threads reviewed

| Thread | Hot-path blocking analysis |
|---|---|
| Main / Qt GUI | UI polls backend via atomics (FPS, algo-us, ring ratio stats), `getMonitoringValidFrames()` / `getMonitoringInvalidFrames()` on a 500 ms timer, `getLatestSnapshot()` at display FPS (≤60 Hz). No locks held across Qt event loop boundaries. |
| Capture (`CaptureService::run`) | Blocks only on `camera->grabFrame()` and the `FrameStore` mutex; per-frame callback is `nullptr` (see `AppBackend::initialize`), so no UI code runs on this thread. |
| Processing workers (`workerLoop`) | Generic queue, not currently on the hot path for realtime — realtime loop carries the work. |
| Realtime processing (`realtimeLoop`) | **See findings below.** |
| Autofocus (`controlLoop`) | Dedicated thread at ~20 Hz; serial COM I/O; contests `ringRatioMutex_` with the realtime thread only while clearing the buffer after a voltage step. |
| Trigger (`triggerLoop`) | Waits on `triggerCV_`; wakes on atomic + `notify_one`; pulses TTL via camera SDK. Not touched by the UI thread except via the non-blocking `onTargetGroupResult(true)` from `sortTriggerBtn` / `periodicTriggerBtn`. |
| Syringe pump (per-pump) | UI-driven Modbus polls; fully isolated from capture/processing. |
| Frame-recording (`AppBackend::frameRecordingThread_`) | Active only in recording mode. Copies frames out of `FrameStore`, calls `processingService_->getRealtimeBackgroundGray()` **per frame** which clones the background (possible future optimisation; not on trigger path). |

## Findings

### F1 — Trigger path is already decoupled from the UI thread

Verified that the 2026-04-15 fix is in place in all three realtime paths
(ROI+drop, full+drop, every-frame). The hoisted callback block fires
`TargetGroupCallback` + `RingRatioCallback` **before** taking
`monitoringFramesMutex_`, so the UI thread's 500 ms
`getMonitoringValidFrames()` / `getMonitoringInvalidFrames()` deep-copy
cannot stall the trigger CV notification. UI only touches the trigger
service through:

- Atomic metric getters (`getTriggerCount`, `getLastOnsetUs`,
  `getPulseDurationUs`) — no mutex.
- `setPulseDurationUs` — atomic store.
- `onTargetGroupResult(true)` from the test buttons — atomic store +
  `notify_one`, never blocks the caller.
- Lifecycle (`setCamera`, `start`, `stop`) runs on the capture thread via
  `CameraReadyCallback`, not on the UI thread.

### F2 — Trigger wake-up was serialised behind the ring-ratio callback

Within the hoisted callback block, ring-ratio fired **before**
target-group. `AutofocusService::onRingRatio` takes `ringRatioMutex_`,
pushes onto a `std::deque<double>` (cap 1000), trims it, then calls
`updateStatistics()` which copies the deque into a `std::vector<double>`
and `std::sort`s it — O(n log n) over up to 1000 samples, ~20–50 µs of
work on every valid frame, all on the realtime thread, **before** the
trigger CV notification fires.

This is not a UI-thread coupling per se, but it made the trigger wake-up
latency depend on the autofocus buffer state on every trigger-worthy
frame.

**Fix (this audit):** swap the order in all three realtime paths so
`TargetGroupCallback` fires immediately after `filterProcessedImage`
returns. `RingRatioCallback` runs afterwards on the same thread; ring
ratio is consumed by `AutofocusService` at ~20 Hz, so a few hundred µs of
added staleness is insignificant.

### F3 — Callback mutex paths are uncontested

`ringRatioCallbackMutex_`, `targetGroupCallbackMutex_`, and
`backgroundCaptureCallbackMutex_` each guard only the function-pointer
fields. The setters run once at `AppBackend::initialize`, never during
capture; the realtime thread is the only reader. In practice these are
uncontested and the scoped-locks only exist to make callback swaps safe
for future maintenance.

### F4 — Trigger service thread is a clean consumer

`triggerLoop` waits on `triggerCV_`, wakes on atomic flag + `notify_one`,
reads `camera_` via `std::memory_order_acquire` (atomic pointer), calls
`setTriggerOutput(true)`, busy-waits `pulseDurationUs_`, then
`setTriggerOutput(false)`. No UI-owned state is touched. The pulse
duration is an atomic `int`, and metrics (`triggerCount_`,
`lastOnsetUs_`) are atomics. Adding an end-to-end latency gauge was
deferred in the 2026-04-15 task and remains deferred — the oscilloscope
is the authoritative source.

## Not fixed (tracked for later)

- `AutofocusService::onRingRatio` still sorts on the realtime thread.
  Could be moved behind a dirty flag consumed by `controlLoop`, or the
  sort replaced with an online median structure. Lower priority now that
  target-group fires first.
- `FrameStore::mutex_` is a single `std::mutex` shared by push (capture
  thread) and query (realtime/UI/frame-recording). Contention is bounded
  by small per-call hold times, but a reader/writer lock or a lock-free
  ring would reduce jitter at high fps.
- `AppBackend` frame-recording thread calls
  `processingService_->getRealtimeBackgroundGray()` per frame, which
  clones the background matrix while holding `rtMutex_`. A shared_ptr
  accessor (`getRealtimeBackgroundGraySharedPtr`) would remove the clone.
- `ProcessingService::snapshotMutex_` serialises every realtime frame's
  snapshot publication against `PlaybackPanel`'s overlay read at display
  FPS. Not on the trigger path; hold time is a `mask.clone()` (full
  frame) so at 2000×2000 this is ~4 MB of memcpy per realtime frame.

## Files changed

- `src/backend/services/ProcessingService.cpp` — 3× callback order swap
  (target-group before ring-ratio) in the hoisted callback block.
- `knowledge_map/services/ProcessingService.md` — added callback-order
  gotcha.
- `knowledge_map/services/TriggerService.md` — clarified UI decoupling
  and the new callback order.
- `knowledge_map/current-state/Recent-Work.md` — dated entry.

## Verification

1. Build Debug + Release.
2. Mock-camera run: monitoring histograms still update at 2 Hz; target-
   group classifications still fire the trigger callback (spdlog trace).
3. Hardware run with oscilloscope on the TTL line: measure end-to-end
   frame→trigger latency with and without the ExperimentMonitoringTab
   visible and with autofocus connected but idle; latency should be
   stable and slightly lower than before (~20–50 µs improvement on
   trigger-worthy frames with a populated ring-ratio buffer).
