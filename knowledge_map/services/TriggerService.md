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

## Gotchas

- Camera pointer is non-owning and read via `std::atomic`. If the camera
  disappears (stop), `setCamera(nullptr)` and `stop()` must be called.
  `AppBackend` wires this via `CameraReadyCallback`.
- Not all cameras support `setTriggerOutput`. `ICamera::setTriggerOutput`
  returns `false` by default (see [[../camera/ICamera]]).
- Requires `ProcessingService::enable_target_group` + thresholds to be set.
