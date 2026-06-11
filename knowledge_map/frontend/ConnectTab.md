# ConnectTab

> First tab. User picks a hardware device, a MindVision device, or a mock
> folder, then clicks Start.

**Source:** `src/frontend/tabs/ConnectTab.cpp`,
`include/frontend/tabs/ConnectTab.h`
**Related:** [[../services/CameraControlService]],
[[System-Utilities]] (`DeviceInitManager`),
[[Dialogs]] (`MockConfigDialog`),
[[../camera/_MOC]]

## Responsibility

- List interfaces/devices returned by
  [[../services/CameraControlService]]`::discoverAllCameras` and the
  MindVision-specific list.
- Offer a "Mock camera" entry; opens `MockConfigDialog` to pick folder +
  interval + loop.
- Offer a dedicated MindVision tab so the user can select a MindVision device
  without mixing it into the EGrabber tree.
- Surface health indicators (model name, firmware, interface/device labels).
- Hand the chosen selection to `AppBackend::setHardwareCameraSelection`
  `AppBackend::setMindVisionCameraSelection`, or
  `AppBackend::configureMockCamera`.

## Gotchas

- Device enumeration is done off the UI thread by
  `DeviceInitManager` (see [[System-Utilities]]) to avoid stalls.
- `MIB_CAMERA_MODE=mock` env var forces mock selection without the dialog
  (see [[../build-and-run/Run-Modes]]).
- `MIB_CAMERA_MODE=mindvision` selects the MindVision path on startup when the
  SDK is available; `MIB_MINDVISION_CAMERA_INDEX` and `MIB_MINDVISION_CONFIG`
  refine the startup selection.
