# MindVisionCamera

> MindVision SDK-backed `ICamera` implementation. Used when the user selects a
> MindVision device and the build was configured with `MIB_ENABLE_MINDVISION=ON`.

**Source:** `src/backend/camera/mindvision/MindVisionCamera.cpp`,
`include/backend/camera/mindvision/MindVisionCamera.h`,
`include/backend/camera/mindvision/MindVisionConfig.h` (pure JSON parse + bounds),
`include/backend/camera/mindvision/MindVisionFrameGeometry.h` (pure frame-vs-buffer bounds check)
**Tests:** `tests/backend/mindvision_config_test.cpp`,
`tests/backend/mindvision_frame_geometry_test.cpp`
**Related:** [[ICamera]], [[../services/CameraControlService]],
[[../architecture/AppBackend]], [[../frontend/ConnectTab]], [[EGrabberCamera]]

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

## JSON config parsing (`MindVisionConfig.h`)

The config-file parse + validation is a pure, QtCore-only function shared by
both `MindVisionCamera::applyJsonConfig` and
[[../services/CameraControlService]]`::applyMindVisionJsonToCamera` (the two had
**drifted** — the camera applied ~19 fields, the control service only 7 — and
neither validated bounds):

- `backend::camera::mindvision::parseConfig(QByteArray)` → `ParseResult{ok,
  config, error, warnings}`. Malformed JSON or a non-object root → `ok=false`
  with `error`. Otherwise every numeric field is clamped to a safe range and one
  warning is recorded per clamp.
- Critical clamps: `width`/`height` forced `>= 1` (a non-positive ROI was
  unusable), `exposure_time_us > 0`, and **`strobe_pulse_width_us` /
  `strobe_delay_us` forced `>= 0`** — a negative value previously wrapped to a
  multi-second pulse when cast to the SDK's unsigned type.

Both call sites read the file, call `parseConfig`, log warnings, then apply
`config.*` to the SDK (the camera applies the full set; the control service
still applies only its historical subset).

## Frame-vs-buffer bounds check (`MindVisionFrameGeometry.h`)

`start()` allocates `outBuffer_` once, sized `bufferWidth_ * bufferHeight_`
from a `CameraGetImageResolution` query. `grabFrame()` then reads
`tSdkFrameHead::iWidth/iHeight` fresh from every delivered frame to size both
`CameraImageProcess()`'s write into `outBuffer_` and the `out.data.assign()`
read out of it. Those two numbers come from separate SDK calls at separate
times, so nothing guaranteed a later frame's reported geometry still fit the
buffer sized once at `start()` — the same untrusted-delivery mismatch class
`EGrabberCamera::replenishPendingFrames` guards against for its own
buffer/geometry pair. `grabFrame()` now calls
`backend::camera::mindvision::frameFitsInBuffer(frameHead.iWidth,
frameHead.iHeight, bufferWidth_, bufferHeight_)` before invoking
`CameraImageProcess()`; an oversized frame is logged and skipped (the
capture loop retries) instead of overflowing `outBuffer_`. The check is a
header-only, SDK-independent function (same split as
`MindVisionConfig.h`) so it is unit-testable on Linux even though
`grabFrame()` itself only compiles under `MIB_HAS_MINDVISION` (Windows +
SDK).

## Gotchas

- The SDK is discovered at configure time through `MIB_MINDVISION_SDK_ROOT`
  or the `MIB_MINDVISION_SDK_DIR` environment variable.
- The implementation accepts both SDK include layouts:
  `MindVision/CameraApiLoad.h` and a flat `CameraApiLoad.h` directly under
  the configured include directory.
- `MindVisionCamera.cpp` owns the SDK dynamic-loader definitions by defining
  `API_LOAD_MAIN`; other MindVision users include the SDK header as extern
  declarations only.
- Runtime deployment copies the MindVision DLL next to the app when Windows
  packaging is enabled.
- The current backend treats MindVision as a separate camera provider; mock
  and EGrabber paths remain independent.
