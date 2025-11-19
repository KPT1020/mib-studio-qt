Title: Move Nanopositioner UI into Config tabs
Date: 2025-11-19

Summary:
- Moved the Nanopositioner Autofocus controls out of `frontend/PreviewPage` into a dedicated `frontend/NanopositionerTab`.
- Added the new tab alongside App config and Camera script within `frontend/ConfigTabs`.
- Functionality preserved: connects/disconnects, enable toggle, voltage display and manual +/- with step, periodic status refresh, and status callback.
- Configuration flow unchanged: reads/writes through the same `config.json` path, honoring `QSettings` key `Config/ExternalAppConfigPath`.

Files:
- Added: `include/frontend/NanopositionerTab.h`, `src/frontend/NanopositionerTab.cpp`
- Modified: `src/frontend/ConfigTabs.cpp` (added tab), `include/frontend/PreviewPage.h`, `src/frontend/PreviewPage.cpp` (removed old UI/logic)
- Build: `CMakeLists.txt` updated to include new files

Notes:
- Logging uses `spdlog` (no std::cout).
- EGrabber sample programs were reviewed; no SDK interaction changes required for this re-organization. Existing capture/metrics patterns remain aligned with samples (e.g., 310-high-frame-rate).
- Qt runtime remains unchanged; standard `windeployqt` guidance still applies.



