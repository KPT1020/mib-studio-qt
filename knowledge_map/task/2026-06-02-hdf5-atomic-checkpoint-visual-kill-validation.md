# 2026-06-02 — HDF5 atomic recovery promotion + visual kill validation

## Context

`SIGKILL` crash tests showed that readability-only checks were insufficient:
we also needed to prove that frame payloads were not scrambled, and that
recovery artifacts were never left as active zero-byte files.

## Changes made

1. Hardened `Hdf5Service` recovery handling:
   - Added staged checkpoint flow:
     - `<file>.recovery.h5.tmp` (copy target)
     - previous active checkpoint rotated to `<file>.recovery.h5.bak`
     - temp promoted atomically to `<file>.recovery.h5`
   - Active recovery promotion now avoids truncating the last known-good
     checkpoint during copy/update windows.
   - `loadFile(path)` fallback chain updated to:
     1) primary `path`
     2) `path + ".recovery.h5"`
     3) `path + ".recovery.h5.bak"`
   - Post-write flush + checkpoint update now runs after:
     - `saveFrames`
     - `appendFrames`
     - `writeExperimentInfo`
     - `writeConfigJson`
     - `appendRecordingFrames`
     - `writeRecordingInfo`

2. Added `hdf5_abrupt_stop_tool` backend test utility target:
   - Writer modes:
     - `run-experiment <h5>`
     - `run-recording <h5>`
   - Post-kill validators:
     - `check-checkpoint <h5>` (non-empty recovery checkpoint required)
     - `check-experiment <h5>`
     - `check-recording <h5>`
   - Visual/content checks:
     - samples saved frames from HDF5
     - compares sampled frames to mock source images (MAD threshold)
     - writes contact-sheet preview to `<file>.preview.png`

3. Documentation updates:
   - `knowledge_map/services/Hdf5Service.md`
   - `knowledge_map/build-and-run/Build.md`
   - `knowledge_map/build-and-run/Run-Modes.md`
   - `docs/howto/linux-build.md`
   - `knowledge_map/current-state/Recent-Work.md`

## Validation summary

- Rebuilt backend and validator target with `linux-backend-only` preset.
- `ctest --preset linux-backend-only-test -L backend` passed.
- Repeated `SIGKILL` loops for both experiment and recording modes passed:
  - checkpoint file remained non-empty
  - HDF5 datasets readable
  - sampled frames matched mock references
  - preview contact sheets produced.
