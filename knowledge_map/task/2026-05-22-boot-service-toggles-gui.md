# 2026-05-22 - Boot service toggles via GUI

## Summary

Added a GUI path to configure services disabled at next boot.

## What changed

- Added Settings action: **Boot Service Toggles...**
- Dialog allows selecting startup-disabled services (including `auto_update`)
- Persisted selection in `QSettings` key `Startup/DisabledServices`
- `main.cpp` now reads persisted value and applies it to
  `MIB_DISABLED_SERVICES` before backend startup
- `MainWindow` skips updater creation/startup check when `auto_update` is disabled

## Notes

- Persisted settings apply on **next launch**.
- External `MIB_DISABLED_SERVICES` env var still takes precedence over persisted GUI value.

## Files touched

- `src/frontend/core/MainWindow.cpp`
- `src/frontend/core/main.cpp`
- `knowledge_map/frontend/MainWindow.md`
- `knowledge_map/build-and-run/Run-Modes.md`
- `knowledge_map/current-state/Recent-Work.md`
