# Select external App config and Camera script

This app supports using external files for both the application JSON config and the eGrabber camera script.

## UI
- Open the Config tabs at the bottom of the Preview page.
- App config (config.json):
  - Use Reload / Save as usual.
  - Click Browse… to select an external `config.json`.
  - Click Clear to revert to the default include path.
  - The active path is shown in the label on the right.
- Camera script (egrabberConfig.js):
  - Use Reload / Save / Apply to Camera.
  - Click Browse… to select an external `egrabberConfig.js`.
  - Click Clear to revert to the default include path.
  - The active path is shown in the label on the right.

## Persistence (QSettings)
- External paths persist across sessions via:
  - `Config/ExternalAppConfigPath`
  - `Config/ExternalCameraScriptPath`

## Behavior
- Reload/Save operate on the active path (external when set; otherwise the default include path).
- Apply to Camera saves the editor to the active JS path, then applies it directly using the Euresys SDK (`EGrabber::runScript`).
- Logging is via spdlog.

## Notes (SDK pattern)
- This mirrors Euresys sample patterns (e.g., high frame rate scripts) by running the script directly on the device.


