# MindVisionCamera

> MindVision SDK-backed `ICamera` implementation. Used when the user selects a
> MindVision device and the build was configured with `MIB_ENABLE_MINDVISION=ON`.

**Source:** `src/backend/camera/mindvision/MindVisionCamera.cpp`,
`include/backend/camera/mindvision/MindVisionCamera.h`,
`include/backend/camera/mindvision/MindVisionConfig.h` (pure JSON parse + bounds),
`src/backend/camera/mindvision/MindVisionConfigApply.cpp` +
`include/backend/camera/mindvision/MindVisionConfigApply.h` (shared SDK apply),
`include/backend/camera/common/WindowedRate.h` (host-side rate estimator)
**Tests:** `tests/backend/mindvision_config_test.cpp`,
`tests/backend/mindvision_config_apply_test.cpp`,
`tests/backend/windowed_rate_test.cpp`,
`tests/hardware/hw_mindvision_apply_test.cpp` (real device, skip-77)
**Related:** [[ICamera]], [[../services/CameraControlService]],
[[../architecture/AppBackend]], [[../frontend/ConnectTab]]

## Responsibility

- Own the MindVision camera lifecycle: open, start, grab frame, stop, and
  release the SDK handle.
- Apply an optional JSON config file before capture starts.
- Expose trigger-output helpers used by the backend camera wiring.

## Key APIs

- `MindVisionCamera(int cameraIndex, std::string configPath = {})`
- `applyConfig(const CameraConfig&)`
- `start()` / `stop()`
- `grabFrame(Frame&)`
- `pollStats(CameraStats&)`
- `checkDeviceHealth()`
- `configureTriggerOutput(lineSelector)` / `setTriggerOutput(high)`

## Platform behavior

- **Windows + `MIB_ENABLE_MINDVISION=ON`**: real SDK-backed implementation.
- **Windows + `MIB_ENABLE_MINDVISION=OFF`** and all non-Windows builds: safe
  stub that logs unsupported calls and returns failure/no-op.

## JSON config: parse (`MindVisionConfig.h`) + apply (`MindVisionConfigApply.h`)

The config-file parse + validation is a pure, QtCore-only function:

- `backend::camera::mindvision::parseConfig(QByteArray)` → `ParseResult{ok,
  config, error, warnings}`. Malformed JSON or a non-object root → `ok=false`
  with `error`. Otherwise every numeric field is clamped to a safe range and one
  warning is recorded per clamp.
- Critical clamps: `width`/`height` forced `>= 1` (a non-positive ROI was
  unusable), `exposure_time_us > 0`, and **`strobe_pulse_width_us` /
  `strobe_delay_us` forced `>= 0`** — a negative value previously wrapped to a
  multi-second pulse when cast to the SDK's unsigned type.
- `trigger_output_index` (0–2, default 1 = OUT2) selects the GPIO line used
  for the sort trigger pulse.

The SDK application of the parsed config is likewise a single shared path,
`applyJsonFileToCamera(hCamera, jsonPath)` → `ApplyResult{ok, config, error,
warnings}` in `MindVisionConfigApply.{h,cpp}`. Both call sites route through
it — `MindVisionCamera::applyJsonConfig` (before `CameraPlay` in `start()`)
and [[../services/CameraControlService]]`::applyMindVisionConfig` (runtime
apply on a fresh non-streaming handle). The two sites previously carried
duplicated apply code that **drifted** (the service applied only a 7-field
subset); the shared function applies every field. `kConfigFieldCount` in
`MindVisionConfig.h` is static_asserted in the apply .cpp so a new `Config`
field cannot silently miss the apply sequence.

## Gotchas

- The SDK is discovered at configure time through `MIB_MINDVISION_SDK_ROOT`
  or the `MIB_MINDVISION_SDK_DIR` environment variable.
- The implementation accepts both SDK include layouts:
  `MindVision/CameraApiLoad.h` and a flat `CameraApiLoad.h` directly under
  the configured include directory.
- `MindVisionCamera.cpp` owns the SDK dynamic-loader definitions by defining
  `API_LOAD_MAIN`; other MindVision users (`CameraControlService.cpp`,
  `MindVisionConfigApply.cpp`) include the SDK header as extern declarations
  only.
- The shared apply precondition: the handle must come from `CameraInit` with
  `CameraPlay` NOT active. Both call sites guarantee this; do not call it on
  a streaming camera.
- `checkDeviceHealth` must never consume from the image queue — it previously
  did a probe `CameraGetImageBuffer` that silently discarded one frame per
  check (in trigger mode possibly a triggered target frame) and stalled the
  capture loop; it now uses `CameraGetFrameStatistic`.
- The trigger path (`configureTriggerOutput`/`setTriggerOutput`) must never
  share `stateMutex_` — `grabFrame` holds it across `CameraImageProcess` and
  `stop()` holds it across teardown, which would jitter the sort pulse. A
  dedicated `triggerMutex_` guards `triggerCameraHandle_`, which `stop()`
  invalidates before `CameraUnInit` (mirrors [[EGrabberCamera]]).
- `pollStats` rates are **host-computed over the poll window** via
  `WindowedRate` (the MVSDK reports no device-side rates); EGrabber reports
  device-side instantaneous rates. A cumulative average would go stale and
  dilute toward zero in trigger mode.
- `CameraConfig.bufferPartCount/numBuffers` are EGrabber DMA-tuning knobs and
  are intentionally not applied — the MVSDK owns its internal buffer queue.
- `Frame.timestamp` units differ per vendor (MVSDK `uiTimeStamp × 100'000`
  vs EGrabber device ticks); downstream treats device timestamps as opaque
  and uses `hostTimestampUs` for latency math.
- Runtime deployment copies the MindVision DLL next to the app when Windows
  packaging is enabled.
- The current backend treats MindVision as a separate camera provider; mock
  and EGrabber paths remain independent.
