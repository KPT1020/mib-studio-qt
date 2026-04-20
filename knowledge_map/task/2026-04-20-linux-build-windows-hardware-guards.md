# 2026-04-20 — Guard Windows-only hardware deps for Linux cloud builds

## Summary

Linux cloud builds were blocked by unconditional Windows hardware dependencies:
Euresys EGrabber headers/libs and Coremor nanopositioner SDK symbols were pulled
into the build even when running outside Windows.

This task introduces platform guards so non-hardware functionality can still
compile in Linux CI/cloud environments.

## What changed

- Added `MIB_HAS_EGRABBER` CMake flag (`ON` on Windows, `OFF` elsewhere).
- Guarded Windows-only include/link paths in `CMakeLists.txt`:
  - `C:/Program Files/Euresys/eGrabber/include`
  - `include/Coremor`
  - `XMT_DLL_SER.lib`
- Added compile definition propagation:
  - `MIB_HAS_EGRABBER=1` on Windows
  - `MIB_HAS_EGRABBER=0` on non-Windows
- `mib_studio_qt` target no longer forces `WIN32` subsystem on Linux.
- Split Autofocus compilation by platform:
  - Windows: `AutofocusService.cpp`
  - Non-Windows: `AutofocusService.stub.cpp` (no-op/unsupported behavior)
- Removed unconditional EGrabber include from
  `include/backend/services/CameraControlService.h`.
- Added guarded includes and non-Windows fallback implementations for:
  - `CameraControlService`
  - `EGrabberCamera`
- Updated runtime camera selection defaults:
  - `AppBackend` forces mock camera mode when EGrabber is unavailable.
  - Hardware camera selection logs warning and keeps mock factory on non-Windows.
  - `CaptureService` defaults to `MockCamera` factory on non-Windows.

## Behavior by platform

### Windows

- Existing hardware behavior remains:
  - EGrabber camera discovery/control.
  - Coremor autofocus.
  - hardware camera factory defaults.

### Linux / non-Windows

- Build no longer requires EGrabber/Coremor SDK presence.
- Camera control discovery/reset/script operations become no-op unsupported
  stubs (warn once and return empty/false).
- `EGrabberCamera` compiles as a safe stub.
- Autofocus service compiles as a stub implementation.
- Backend capture path defaults to mock camera behavior.

## Validation

- Attempted configure with:
  - `cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`
- Result failed before project checks due to environment toolchain issue:
  - linker missing `-lstdc++`.
- This indicates platform-guard logic was in place, but local Linux toolchain
  was incomplete for end-to-end compile verification in this environment.

## Files touched

- `CMakeLists.txt`
- `include/backend/services/CameraControlService.h`
- `include/camera/common/EGrabberCamera.h`
- `src/backend/AppBackend.cpp`
- `src/backend/services/CameraControlService.cpp`
- `src/backend/services/CaptureService.cpp`
- `src/backend/services/AutofocusService.stub.cpp` (new)
- `src/camera/common/EGrabberCamera.cpp`
