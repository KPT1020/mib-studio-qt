# UI: Menu bar and Processing Settings dialog

Date: 2025-11-13

Summary:
- Added a standard menu bar to `MainWindow` with File, Settings, and Help.
- Moved "Invalid frame sampling" and "Flush interval" from the capture toolbar into a new modal `ProcessingSettingsDialog` opened via Settings.
- Kept the Capture toolbar limited to start/stop camera and experiment actions.

Details:
- File → Exit closes the app.
- Settings → Processing Settings… opens the dialog with two `QSpinBox` controls:
  - Invalid frame sampling: 1..10000 (every Nth invalid frame saved)
  - Flush interval: 1..10000 (frames per HDF5 flush)
- Help → About shows a basic app description.
- Dialog supports OK, Cancel, and Apply. Apply/OK immediately updates backend via `ProcessingService::setInvalidFrameSamplingRate` and `setFlushInterval`. Changes are logged with spdlog.

Files:
- `src/frontend/MainWindow.cpp`, `include/frontend/MainWindow.h`
- `src/frontend/ProcessingSettingsDialog.cpp`, `include/frontend/ProcessingSettingsDialog.h`
- `CMakeLists.txt` (added dialog sources to `FRONTEND_COMMON_SOURCES`)

Notes:
- No changes to capture or processing algorithms.
- eGrabber sample programs were reviewed; no SDK interaction changes were required for this UI re-organization. Existing capture and processing patterns remain aligned with sample best practices (e.g., high-rate processing as in 310-high-frame-rate). 


