# TriggerService

> Emits a short high pulse on a configurable camera digital-output line
> at a fixed delay after frame capture, when [[ProcessingService]]
> classifies a frame as "target group".

**Source:** `src/backend/services/TriggerService.cpp`,
`include/backend/services/TriggerService.h`
**Related:** [[ProcessingService]], [[CaptureService]],
[[../data-model/FrameStore]], [[../camera/ICamera]]

## Responsibility

- Hold a non-owning `ICamera*` (handed to it by
  [[CaptureService]]::CameraReadyCallback via
  [[../architecture/AppBackend]]).
- On `onTargetGroupResult(true, captureObserved)`, enqueue a pending
  trigger with deadline `captureObserved + triggerDelayUs_`. A dedicated
  thread waits until the deadline, then raises the configured output
  line, busy-waits `pulseDurationUs_`, then lowers it.
- Expose metrics: `getTriggerCount`, `getLastOnsetUs` (legacy onset latency
  of the `setTriggerOutput(true)` call itself), `getLastRealizedDelayUs`
  (time from capture to pulse onset — ideally equals `triggerDelayUs_`),
  `getLastSlipUs` (how much the deadline was missed by), and
  `getDroppedTriggers` (count of hits dropped due to full pending queue).
  `resetMetrics` zeros all counters.

## Key APIs / Entry points

- `onTargetGroupResult(bool isTargetGroup, std::chrono::steady_clock::time_point captureObserved)`
  — invoked by [[ProcessingService]]'s `TargetGroupCallback`. The
  `captureObserved` value is the steady_clock time recorded in
  [[CaptureService]] immediately after `grabFrame()` returns, propagated
  through the [[../data-model/FrameStore]] sideband ring.
- `setPulseDurationUs(us)` — pulse width (default 1 µs, busy-wait).
- `setTriggerDelayUs(us)` — fixed delay from capture to pulse onset
  (default 0 µs; regression-safe).

## Threading

Dedicated thread waiting on `triggerCV_`. Two waits per iteration:
1. `wait()` until the queue becomes non-empty (or shutdown).
2. `wait_until(front().deadline)` until the front deadline arrives (or
   shutdown).

Both waits hold `triggerMutex_`. The queue is a `std::deque<PendingTrigger>`
bounded to `kMaxPendingTriggers = 32`; on overflow the oldest entry is
dropped with a WARN log and `droppedTriggers_` increments.

## Design rationale

Previously the service fired the pulse immediately when
`onTargetGroupResult(true)` was signalled — which happens at the trailing
edge of [[ProcessingService]]'s realtime pipeline (blur, morphology,
contour detection, classification). Processing time is variable (1–10 ms
per frame), so onset-from-capture jitter tracked processing jitter 1:1.

Now onset is scheduled from `captureObserved` (set in [[CaptureService]]
right after `grabFrame()`), so pulse timing is independent of downstream
processing cost. Slip occurs only when `processing_time + scheduler_wake
> triggerDelayUs_`, in which case the pulse fires immediately and the
slip amount is logged and recorded in `lastSlipUs_`.

## Gotchas

- Camera pointer is non-owning and read via `std::atomic`. If the camera
  disappears (stop), `setCamera(nullptr)` and `stop()` must be called.
  `AppBackend` wires this via `CameraReadyCallback`.
- Not all cameras support `setTriggerOutput`. `ICamera::setTriggerOutput`
  returns `false` by default (see [[../camera/ICamera]]).
- EGrabber does not expose a hardware-scheduled digital output; scheduling
  is done in software by the trigger thread. Slip is therefore bounded by
  OS scheduler granularity (~tens of µs on a typical Windows install).
- Requires `ProcessingService::enable_target_group` + thresholds to be set.
- The manual UI test button in [[../frontend/ExperimentMonitoringTab]]
  passes `steady_clock::now()` as `captureObserved`, so it honours
  `triggerDelayUs_` exactly like a real target-group hit.
- `triggerDelayUs_ = 0` preserves the legacy "fire as soon as possible
  after classification" behavior (deadline equals capture time, which is
  always in the past by the time classification finishes).
