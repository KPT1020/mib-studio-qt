# Camera Reset Behavior

- Action: Issues GenICam SFNC `DeviceReset` to the selected hardware camera.
- UI: Button "Reset Camera" in `ConfigTabs` → "Camera script (egrabberConfig.js)" tab.
- Backend:
  - `backend::services::CameraControlService::deviceReset(if, dev)`
  - Wrapper: `backend::AppBackend::resetSelectedHardwareCamera()`
  - Best-effort `AcquisitionStop` before reset
  - Logging via spdlog
- Effects:
  - Capture is stopped and remains stopped
  - Camera may briefly disconnect and require a refresh in the Connect tab
  - After reset, re-apply camera script if needed and restart capture


