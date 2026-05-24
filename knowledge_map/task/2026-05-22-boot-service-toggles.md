# 2026-05-22 - Boot-time service toggles

## Summary

Expanded boot-time startup toggles via `MIB_DISABLED_SERVICES` so backend and
startup UI services can be selectively disabled without changing binaries.

## Scope

- `sqlite` bootstrap (`SqliteService::initialize`)
- `hdf5` bootstrap (`Hdf5Service::initialize`)
- `processing` bootstrap (`ProcessingService::loadEModulusLut` + `start`)
- `yolo` bootstrap (`YoloService::initialize`)
- `autofocus` startup wiring (processing ring-ratio callback)
- `trigger` startup wiring (processing target-group callback + camera lifecycle callback)
- `capture` (`camera`) startup wiring and camera factory bootstrap
- `playback` startup `FrameStore` wiring
- `auto_update` startup behavior in `MainWindow` (disable updater construction/check)

Tokens are comma-separated and case-insensitive; `all` disables all supported
backend startup paths.

## Notes

- Services are still constructed to preserve existing backend/frontend references.
- This is a startup wiring control, not a full runtime feature-flag system.
- `auto_update` is handled in frontend (`MainWindow`), while other tokens are handled
  in backend startup (`AppBackend`).

## Files touched

- `src/backend/AppBackend.cpp`
- `src/frontend/core/MainWindow.cpp`
- `knowledge_map/architecture/AppBackend.md`
- `knowledge_map/frontend/MainWindow.md`
- `knowledge_map/build-and-run/Run-Modes.md`
- `knowledge_map/current-state/Recent-Work.md`
