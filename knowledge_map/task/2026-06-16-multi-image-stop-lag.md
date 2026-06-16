# 2026-06-16 — Multi-image flush/stop lag hardening

## Context

In multi-image mode (`multi_image_count=15`), users reported:

- first periodic flush succeeds, then later flushes become much slower
- stopping an experiment can stall the UI for a long time

## Root cause summary

Two costs compounded during multi-image runs:

1. `Hdf5Service` copied the full `.h5` to `.recovery.h5` after every append
   flush, so checkpoint cost grew with file size.
2. Series-image appends used many tiny HDF5 writes (`N * seriesCount`) instead
   of writing one packed record per frame.

Stop lag was amplified because experiment accumulation remained active until
after final stop-time flush calls started.

## Changes

- `Hdf5Service` recovery checkpoints are now throttled on append paths
  (`appendFrames`, `appendRecordingFrames`) by time/size gates, while
  metadata/finalization writes (`writeExperimentInfo`, `writeConfigJson`,
  `writeRecordingInfo`) still force a checkpoint update.
- Multi-image series writes now pack each frame's full series into one HDF5
  write call per frame record.
- `MainWindow::onStopExperiment()` and
  `ExperimentController::stopExperiment()` now call `ProcessingService::endExperiment()`
  before the final synchronous flush/write path to prevent new-frame buildup
  during shutdown I/O.
- Added regression test:
  `tests/backend/hdf5_recovery_checkpoint_throttle_test.cpp`.

## Validation

- `cmake --build --preset linux-backend-only-build`
- `ctest --preset linux-backend-only-test -R "backend\\.hdf5_recovery_checkpoint_throttle|backend\\.processing_multi_object|recording\\.lifecycle|backend\\.smoke" --output-on-failure`

## Remaining risk

- Full Linux GUI build could not run in this cloud environment because required
  Qt component `Qt6Charts` is missing from system packages.
