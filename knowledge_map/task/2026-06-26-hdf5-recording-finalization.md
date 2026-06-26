# HDF5 recording finalization hardening

## Context

Recording-mode `.h5` files from the 20260611 Cell focus batch opened only
after `h5clear --increment`, indicating the physical file grew past the EOA
stored in the HDF5 superblock. That points at an interrupted or failed final
flush/close after `/recorded_frames` and `/recording_info` writes.

## Changes

- `Hdf5Service::openFile` and `loadFile` use a file-access property list with
  `H5F_CLOSE_STRONG`, so leaked HDF5 IDs cannot prevent file finalization.
- `Hdf5Service::closeFile` explicitly runs `H5Fflush(..., H5F_SCOPE_GLOBAL)`
  for writable files before `H5Fclose`, then logs final flush status, close
  timing, and open-object count.
- `AppBackend::startFrameRecording` increments `frameRecordingWritten_` only
  after `appendRecordingFrames` succeeds and logs a failure if
  `/recording_info` cannot be written.
- **Removed the `.recovery.h5` checkpoint entirely** (method, all call sites,
  the `openFile` stale-checkpoint cleanup, and the `loadFile` fallback). The
  per-append full-file copy dominated NAS save time and starved the recorder
  thread, dropping frames. There is no recovery sidecar; `loadFile` opens the
  primary only.
- Added `Hdf5Service::maybeIntervalFlush()`: append hot paths
  (`appendFrames`, `appendRecordingFrames`) flush at most once per
  `MIB_HDF5_FLUSH_INTERVAL_MS` (default 5000 ms). One-shot finalization writes
  (`writeExperimentInfo`, `writeConfigJson`, `writeRecordingInfo`) keep an
  unconditional flush; `closeFile` keeps the final flush + strong close.
- **Accepted risk:** a crash/power-loss mid-recording can lose up to one flush
  interval and may require `h5clear --increment` (or be unrecoverable).

## Verification

- Run `ctest --preset linux-backend-only-test -R recording.lifecycle` to check
  recording files still round-trip through the primary `.h5` path.
- Run `ctest --preset linux-backend-only-test -R recording.hdf5_resilience`
  for forced (destructor) finalization, clean-fail on a corrupted primary, and
  pixel/metadata preservation across recording-mode and standard experiment
  HDF5 files.
- Run `ctest --preset linux-backend-only-test -R recording.hdf5_save_performance`
  for a non-flaky save-performance smoke guard over repeated recording appends.
  Override the broad default timeout with `MIB_HDF5_PERF_MAX_MS` when profiling
  a specific machine; tune flush cadence with `MIB_HDF5_FLUSH_INTERVAL_MS`.
- Run `python3 scripts/check_docs.py` for vault link integrity.
