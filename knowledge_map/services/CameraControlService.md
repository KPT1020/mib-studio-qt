# CameraControlService

> Device discovery and one-shot parameter application for the hardware camera
> backends. Does **not** own the acquisition thread - that's [[CaptureService]].

**Source:** `src/backend/services/CameraControlService.cpp`,
`include/backend/services/CameraControlService.h`
**Related:** [[../camera/EGrabberCamera]], [[../camera/MindVisionCamera]],
[[CaptureService]], [[../frontend/ConnectTab]]

## Responsibility

- `discoverCameras()` / `discoverFramegrabbers()` — enumerate Euresys
  interfaces, devices, and streams. Returns `DiscoveredCamera` /
  `DiscoveredFramegrabber` with index triples and human labels.
- `discoverMindVisionCameras()` / `discoverAllCameras()` — enumerate
  MindVision devices and merge them with the EGrabber list for the connect UI.
- `applyMindVisionConfig(cameraIndex, configPath, errorOut)` — apply a JSON
  config to a selected MindVision device. Routes through the shared
  `applyJsonFileToCamera` helper (`MindVisionConfigApply.h`, see
  [[../camera/MindVisionCamera]]) so the **full** field set is applied — the
  historical 7-field subset here was drift and is gone. Safe to apply
  everything (incl. ROI/resolution) because [[../architecture/AppBackend]]
  stops capture first and this service opens a fresh non-streaming
  `CameraInit` handle, applies, then `CameraUnInit`s.
- `applyScriptToDevice(ifIdx, devIdx, scriptPath, errorOut)` — push a
  GenICam JS file to a specific device.
- `deviceReset(ifIdx, devIdx, errorOut)` — issue SFNC `DeviceReset`;
  best-effort stops acquisition first.

## Threading

Synchronous, called from main thread.

## Platform behavior

- **Windows (`MIB_HAS_EGRABBER=1`)**: full EGrabber-backed discovery, script
  apply, and device reset.
- **Windows (`MIB_HAS_MINDVISION=1`)**: MindVision discovery and config
  application are enabled when `MIB_ENABLE_MINDVISION=ON` and the SDK can be
  located at configure time.
- **Non-Windows / disabled SDK builds**: methods compile as safe fallbacks:
  discovery returns empty vectors; mutating operations return `false` and can
  populate `errorOut`.

## Gotchas

- Applying a script **stops capture** as a side effect (done in
  [[../architecture/AppBackend]]). Capture does not auto-restart.
- Mock cameras are not discovered here — they're configured via
  `AppBackend::configureMockCamera`.
- MindVision selection is handled separately from EGrabber selection in
  [[../frontend/ConnectTab]] and `AppBackend`.
- MindVision SDK loader symbols are defined by
  [[../camera/MindVisionCamera]]; this service and `MindVisionConfigApply.cpp`
  include the SDK header without `API_LOAD_MAIN` to avoid duplicate loader
  definitions.
- `deviceReset` is EGrabber-only: with a MindVision camera selected,
  `AppBackend::resetSelectedHardwareCamera` reports "Device reset is only
  supported for EGrabber cameras" (the MVSDK has no SFNC `DeviceReset`;
  recovery is reselecting in the Connect tab).
- See `docs/howto/external-config-and-camera-script.md` for script format
  and `docs/integration/egrabber.md` for SDK notes.
