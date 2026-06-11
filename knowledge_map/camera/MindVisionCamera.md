# MindVisionCamera

> MindVision SDK-backed `ICamera` implementation. Used when the user selects a
> MindVision device and the build was configured with `MIB_ENABLE_MINDVISION=ON`.

**Source:** `src/backend/camera/mindvision/MindVisionCamera.cpp`,
`include/backend/camera/mindvision/MindVisionCamera.h`
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

## Gotchas

- The SDK is discovered at configure time through `MIB_MINDVISION_SDK_ROOT`
  or the `MIB_MINDVISION_SDK_DIR` environment variable.
- Runtime deployment copies the MindVision DLL next to the app when Windows
  packaging is enabled.
- The current backend treats MindVision as a separate camera provider; mock
  and EGrabber paths remain independent.
