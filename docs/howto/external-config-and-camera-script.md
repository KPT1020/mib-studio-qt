# Select external App config and Camera script

This app supports using external files for both the application JSON config and the eGrabber camera script.

## UI
- Open the Config tabs at the bottom of the Preview page.
- App config (config.json):
  - Use Reset / Save as usual.
  - Click Browse… to select an external `config.json`.
  - Click Clear to revert to the default include path.
  - The active path is shown in the label on the right.
- Camera script (egrabberConfig.js):
  - Use Reset / Save / Apply to Camera.
  - Click Browse… to select an external `egrabberConfig.js`.
  - Click Clear to revert to the default include path.
  - The active path is shown in the label on the right.

## Persistence (QSettings)
- External paths persist across sessions via:
  - `Config/ExternalAppConfigPath`
  - `Config/ExternalCameraScriptPath`

## Behavior
- Reset/Save operate on the active path (external when set; otherwise the default include path).
- Apply to Camera saves the editor to the active JS path, then applies it directly using the Euresys SDK (`EGrabber::runScript`).
- Logging is via spdlog.

## Profiles
- Profiles let users save and switch configurations quickly.
- Storage: `../include/profiles/<profileName>/config.json` (+ optional `egrabberConfig.js`)
- Use the Profile combo to select a profile, Save Profile to create/update, and Delete/Rename to manage.
- When loading a profile, the app sets the external paths to the profile files and resets the editors. The live watcher applies changes automatically.

## Notes (SDK pattern)
- This mirrors Euresys sample patterns (e.g., high frame rate scripts) by running the script directly on the device.




