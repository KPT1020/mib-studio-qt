# EGrabberCamera

> Production [[ICamera]] implementation backed by the Euresys EGrabber SDK.

**Source:** `src/camera/common/EGrabberCamera.cpp`,
`include/camera/common/EGrabberCamera.h`
**Related:** [[ICamera]], [[../services/CaptureService]],
[[../services/CameraControlService]]

## Responsibility

- Wrap an `Euresys::EGrabber` handle scoped to a specific
  `(interfaceIndex, deviceIndex)` selection.
- Implement `start`, `stop`, `grabFrame`, `pollStats`.
- Expose `configureTriggerOutput` + `setTriggerOutput` for
  [[../services/TriggerService]].
- Surface `checkDeviceHealth()` to detect camera disappearance.
- On non-Windows builds (`MIB_HAS_EGRABBER=0`), compile as a safe stub that
  logs unsupported hardware start and returns `false`/no-op for camera I/O.

## Key reference material

- **Euresys SDK samples** — `egrabber-sample-programs/` in this repo
  (shipped as a reference source tree). Always check for a ready-made
  pattern before rolling your own SDK call. See
  [[../conventions/Code-Conventions]].
- **Integration note** — `docs/integration/egrabber.md`.
- **Start/stop safety** — `docs/howto/safe-start-stop-egrabber.md` and
  task `knowledge_map/task/2025-11-14-safe-start-stop-egrabber.md`.
- **Device reset flow** — task `knowledge_map/task/camera-reset.md`.

## Gotchas

- **StreamModule counters** must be refreshed before stopping capture or
  the final `frameRate` / `dataRate` read will be zero. See
  `fps_mbs_zero.md` task and [[../conventions/Code-Conventions]].
- GenICam `DeviceReset` is best-effort; EGrabber will temporarily lose the
  device handle — [[../services/CameraControlService]] handles this.
- Pixel format mapping: EGrabber PFNC codes flow through unchanged in
  `Frame::pixelFormat`.
- Header and source are guarded by `MIB_HAS_EGRABBER`; non-Windows compile must
  not include `EGrabber.h`.
- **Trigger-thread lifetime protocol:** `setTriggerOutput` runs on the
  [[../services/TriggerService]] thread and takes `triggerMutex_` (never
  `stateMutex_`, which `stop()` holds across ~360 ms of teardown sleeps).
  Every assignment/reset of `grabber_` also holds `triggerMutex_`, so a
  trigger pulse racing a camera stop fails cleanly instead of dereferencing a
  destroyed grabber. `running_` is `std::atomic<bool>` for the same reason
  (read lock-free by the trigger thread and `isRunning()`).
