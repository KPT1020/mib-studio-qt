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
- When no external app config and no active profile are selected, the active bundled/default `config.json` shows `Using default config` in the Config tabs. Experiment start, frame recording start, and camera apply/reset paths are blocked until the displayed default config hash is confirmed, or until an external config/profile is loaded.
- Logging is via spdlog.

## Profiles
- Profiles let users save and switch configurations quickly.
- Storage: `../include/profiles/<profileName>/config.json` plus
  `profile.meta.json` and optional `egrabberConfig.js`.
- Use the Profile combo plus Save Profile, Delete, Rename, Check Updates,
  Update Selected, Show Diff, and Duplicate as Local to manage profiles.
- Local profiles without metadata still load; the app lazily creates a
  `profile.meta.json` file on scan and keeps it updated for local changes.
- When loading a profile, the app sets the external paths to the profile
  files and resets the editors. The live watcher applies changes
  automatically.
- Remote-managed profiles come from the public R2 catalog and can be updated
  manually after a catalog check.

## Notes (SDK pattern)
- This mirrors Euresys sample patterns (e.g., high frame rate scripts) by running the script directly on the device.



