# 2026-05-22 - Boot-time service toggles

## Summary

Added boot-time startup toggles in `AppBackend::initialize` using
`MIB_DISABLED_SERVICES` so selected service startup paths can be disabled
without code changes.

## Scope

- `sqlite` bootstrap (`SqliteService::initialize`)
- `hdf5` bootstrap (`Hdf5Service::initialize`)
- `processing` bootstrap (`ProcessingService::loadEModulusLut` + `start`)
- `yolo` bootstrap (`YoloService::initialize`)

Tokens are comma-separated and case-insensitive; `all` disables all supported
bootstraps.

## Notes

- Services are still constructed to preserve existing backend/frontend references.
- This is a startup wiring control, not a full runtime feature-flag system.

## Files touched

- `src/backend/AppBackend.cpp`
- `knowledge_map/architecture/AppBackend.md`
- `knowledge_map/build-and-run/Run-Modes.md`
- `knowledge_map/current-state/Recent-Work.md`
