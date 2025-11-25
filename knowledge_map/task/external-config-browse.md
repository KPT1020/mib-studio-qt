Title: Enable external “Browse…” for App config and eGrabber script

Context
- Allow selecting external files for `config.json` and `egrabberConfig.js` via UI.
- Persist external selections across sessions using QSettings keys.
- Apply camera scripts directly from the selected external JS (no copy).

Implementation
- UI: Added Browse…/Clear buttons in `ConfigTabs` for App config and Camera script.
- QSettings keys:
  - `Config/ExternalAppConfigPath` (JSON)
  - `Config/ExternalCameraScriptPath` (JS)
- Path resolution:
  - JSON/JS Reset/Save use the current path (external if set; otherwise default include path).
  - “Apply to Camera” saves and applies from the current JS path.
- `PreviewPage::configPath()` returns the external JSON path when set; else falls back to app include path.

Logging
- All info/warn/error routed through spdlog.

Notes
- eGrabber integration uses `EGrabber::runScript(path)`; aligns with Euresys SDK sample patterns (e.g., high frame rate examples).

Usage
- In Config tabs:
  - App config (config.json): Reset, Save, Browse…, Clear. Path label shows the active file.
  - Camera script (egrabberConfig.js): Reset, Save, Apply to Camera, Browse…, Clear. Path label shows the active file.




