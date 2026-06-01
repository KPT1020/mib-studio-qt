# 2026-06-01 — HDF5 crash-recovery checkpoints

## Context

Users reported that an application crash during experiment/recording writes can
leave the active `.h5` unreadable, causing already captured data to become
inaccessible.

## Changes

- Hardened `Hdf5Service` write flow to create a rolling checkpoint sidecar
  after each successful flush:
  - `appendFrames(...)` flushes and copies to `<file>.recovery.h5`
  - `appendRecordingFrames(...)` flushes and copies to `<file>.recovery.h5`
  - `writeExperimentInfo(...)`, `writeConfigJson(...)`, and
    `writeRecordingInfo(...)` also flush + checkpoint
- `loadFile(path)` now attempts `path + ".recovery.h5"` automatically if the
  primary file open fails.
- Recording metadata writes now use open-or-create semantics for
  `/recording_info` attributes, avoiding failures when metadata paths already
  exist.
- `MainWindow::onStopExperiment()` now explicitly flushes HDF5 before final
  experiment metadata writes.

## Risk / trade-offs

- Writing a full `.recovery.h5` copy after each flush increases disk I/O and
  can add latency on very large files.
- The checkpoint strategy prioritizes data recoverability over write throughput.

## Validation

- Build validation could not run in this cloud image because required build
  tools/toolchain setup are missing (`make`/`ninja`, configured C++ compiler,
  and Conan-generated toolchain files).
- Presets attempted:
  - `cmake --preset linux-system-release` (failed: Ninja missing / compiler not configured)
  - `cmake --preset linux-release` (failed: missing `build/linux/conan_toolchain.cmake` and build tool)
