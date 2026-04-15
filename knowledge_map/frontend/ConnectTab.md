# ConnectTab

> First tab. User picks a hardware device or a mock folder, then clicks
> Start.

**Source:** `src/frontend/tabs/ConnectTab.cpp`,
`include/frontend/tabs/ConnectTab.h`
**Related:** [[../services/CameraControlService]],
[[System-Utilities]] (`DeviceInitManager`),
[[Dialogs]] (`MockConfigDialog`),
[[../camera/_MOC]]

## Responsibility

- List interfaces/devices returned by
  [[../services/CameraControlService]]`::discoverCameras`.
- Offer a "Mock camera" entry; opens `MockConfigDialog` to pick folder +
  interval + loop.
- Surface health indicators (model name, firmware, interface/device labels).
- Hand the chosen selection to `AppBackend::setHardwareCameraSelection`
  or `AppBackend::configureMockCamera`.

## Gotchas

- Device enumeration is done off the UI thread by
  `DeviceInitManager` (see [[System-Utilities]]) to avoid stalls.
- `MIB_CAMERA_MODE=mock` env var forces mock selection without the dialog
  (see [[../build-and-run/Run-Modes]]).
