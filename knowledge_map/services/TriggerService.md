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
- On `onTargetGroupResult(true)`, signal the trigger thread to raise the
  configured output line, sleep `pulseDurationUs_`, then lower it.
- Expose metrics: `getTriggerCount`, `getLastOnsetUs`, `resetMetrics`.

## Threading

Dedicated thread waiting on `triggerCV_`. Pulse duration is configurable in
microseconds (default 1 µs).

## Manual & periodic test paths

- Manual single pulse: `sortTriggerBtn` in
  [[../frontend/ExperimentMonitoringTab]] calls
  `onTargetGroupResult(true)` once.
- Periodic test pulses: `periodicTriggerBtn` + `periodicTriggerIntervalSpin`
  in the same tab arm a `QTimer` that calls `onTargetGroupResult(true)`
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
  end-to-end latency use an oscilloscope on the TTL line. See
  [[ProcessingService]] "Callback ordering invariant" — the callback is
  dispatched outside `monitoringFramesMutex_` to keep wake-up latency flat.
