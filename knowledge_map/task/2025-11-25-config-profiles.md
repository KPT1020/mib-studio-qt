Title: Config Profiles - Save/Load per-user settings

Scope
- Add profile management to Config tabs to save and load `config.json` and optionally `egrabberConfig.js`.
- Profiles stored under `../include/profiles/<profileName>/` (project-relative).
- Remember last selected profile and option to include JS via QSettings.

Implementation
- UI: Combo (profileSelect) + Save/Delete/Rename buttons in App config row; checkbox in JS tab to include JS in profiles.
- Logic: Save pretty JSON (or raw if forced), optional JS; Load sets external paths and triggers reset; Delete and Rename handle active profile safely.
- Live reload integration: switching profiles updates external paths and reuses the existing file watcher.

Notes
- Logging only via spdlog.
- Invalid names sanitized; overwrite confirmations; guard missing files.
- 2026-06-11 update: profile support now includes lazy `profile.meta.json`
  generation, public R2 catalog checks, checksum-verified manual updates,
  backup-on-install, and field-level config diffing.

