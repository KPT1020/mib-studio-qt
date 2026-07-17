# TriggerService

> Emits a short high pulse on a configurable camera digital-output line
> when [[ProcessingService]] classifies a frame as "target group".

**Source:** `src/backend/services/TriggerService.cpp`,
`include/backend/services/TriggerService.h`
**Related:** [[ProcessingService]], [[CaptureService]],
[[../camera/ICamera]]

## Responsibility

- Hold a non-owning `ICamera*` (handed to it by
  [[CaptureService]]::CameraReadyCallback via
  [[../architecture/AppBackend]]).
- On `onTargetGroupResult(signal)`, signal the trigger thread to raise the
  configured output line, sleep `pulseDurationUs_`, then lower it.
- `TargetGroupSignal` carries source identity (`objectId`, `trackId`) plus
  source-frame identity for latency correlation (`frameIndex`,
  `hostTimestampUs` — the host monotonic acquisition stamp) and is used
  for metadata at trigger fire time.
- Expose metrics: `getTriggerCount`, `getLastOnsetUs`, `resetMetrics`.
- When [[../diagnostics/PipelineTimingRecorder]] is enabled, each pulse also
  writes a `TriggerTimingRecord` (request/wake/fire/pulse-done stamps +
  coalesced count) keyed by the source frame, enabling true end-to-end
  acquisition→pulse latency measurement. Pending-request metadata lives
  under `triggerMutex_` (same lock discipline as the lost-wakeup fixes).

## Threading

Dedicated thread waiting on `triggerCV_`. Pulse duration is configurable in
microseconds (default 1 µs).

## Manual & periodic test paths

- Manual single pulse: `sortTriggerBtn` in
  [[../frontend/ExperimentMonitoringTab]] calls
  `onTargetGroupResult(services::TargetGroupSignal{.isTargetGroup=true})` once.
- Periodic test pulses: `periodicTriggerBtn` + `periodicTriggerIntervalSpin`
  in the same tab arm a `QTimer` that calls
  `onTargetGroupResult(services::TargetGroupSignal{.isTargetGroup=true})`
  every N ms. Useful for bring-up / oscilloscope checks without needing
  a running pipeline that classifies real "target group" frames.

## Gotchas

- Camera pointer is non-owning and read via `std::atomic`. If the camera
  disappears (stop), `setCamera(nullptr)` and `stop()` must be called.
  `AppBackend` wires this via `CameraReadyCallback`.
- Not all cameras support `setTriggerOutput`. `ICamera::setTriggerOutput`
  returns `false` by default (see [[../camera/ICamera]]).
- Requires `ProcessingService::enable_target_group` + thresholds to be set.
- `getLastOnsetUs()` measures only the duration of the
  `setTriggerOutput(true)` call, not end-to-end frame→trigger latency. For
  software-observable end-to-end latency enable
  [[../diagnostics/PipelineTimingRecorder]] (`MIB_PIPELINE_TIMING=1`, see
  `docs/howto/pipeline-latency-diagnosis.md`); for electrical ground truth
  use an oscilloscope on the TTL line. See
  [[ProcessingService]] "Callback ordering invariant" — the callback is
  dispatched outside `monitoringFramesMutex_` to keep wake-up latency flat,
  and the target-group callback fires **before** the ring-ratio callback
  so the CV notification is not serialised behind autofocus buffer
  maintenance + sort.
- The trigger thread is fully decoupled from the Qt event loop: it waits
  on `triggerCV_`, wakes on `onTargetGroupResult(signal)`, fires a pulse,
  busy-waits `pulseDurationUs_`, and lowers the line. The UI thread only
  calls `onTargetGroupResult` for the manual `sortTriggerBtn` /
  `periodicTriggerBtn` paths — those are non-blocking by construction. No
  UI mutex is held across a trigger wake-up.
- **Lost-wakeup fix:** `onTargetGroupResult` sets `triggerRequested_` while
  holding `triggerMutex_` (the same mutex guarding the consumer's
  `wait()` predicate) before `notify_one()`. Storing the flag lock-free
  races with the consumer's predicate check — if the flag flips after the
  predicate is evaluated but before the thread blocks, the notification is
  lost and the trigger is delayed until the next request (or dropped under
  bursts). This was the root cause of "variable delay / occasional missed
  trigger." Regression guard: `tests/integration/e2e_trigger_timing_test.cpp`
  (`integration.e2e_trigger_timing`) asserts zero missed pulses and reports
  the request→fire latency distribution under CPU load.
- **Lost-wakeup fix #2 — `stop()` deadlock (the more serious one):** `stop()`
  must also clear `running_` while holding `triggerMutex_` before
  `notify_all()`. `start()`/`stop()` run on every capture start/stop (wired via
  [[CaptureService]]::CameraReadyCallback). If `stop()` cleared `running_`
  lock-free and the just-started trigger thread was between its `wait()`
  predicate check (`running_ == true`) and actually blocking, the notify was
  lost and the thread blocked forever — so `stop()`'s `join()` deadlocked,
  which hung `CaptureService::stop()` and therefore the whole app shutdown /
  camera restart. Intermittent and platform-independent. Found by the
  `integration.e2e_pipeline_stress` lifecycle test (its watchdog pinpointed
  `capture.stop`); that test is the regression guard.
- **Known limitation:** `triggerRequested_` is a single bool, so multiple
  target-group results arriving while the thread is mid-pulse coalesce into
  one fire. Coalesced requests are now visible: the pulse's
  `TriggerTimingRecord.coalesced` counts merged extras (the FIRST pending
  request's identity is kept — it has waited longest). Residual latency
  jitter (tens to ~hundreds of µs under load) is inherent to OS scheduling
  of the busy-wait thread; sub-10 µs determinism would require real-time
  thread priority.
