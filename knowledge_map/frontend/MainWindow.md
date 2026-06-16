# MainWindow

> `QMainWindow` subclass at the root of the UI. Holds a reference to
> `backend::AppBackend&` and owns the tab widget, sidebar, and corner
> actions.

**Source:** `src/frontend/core/MainWindow.cpp`,
`include/frontend/core/MainWindow.h`
**Related:** [[Controllers]], [[ConnectTab]], [[../architecture/AppBackend]],
[[System-Utilities]] (`AutoUpdater`, `DeviceInitManager`, `SidebarWidget`,
`PlaybackPanel`)

## Slots and lifecycle

- `onStartCapture` / `onStopCapture` — start/stop camera acquisition via
  [[Controllers]] `CameraController`.
- `onStartExperiment` / `onStopExperiment` — drive
  [[Controllers]] `ExperimentController`. Stop path now issues an explicit
  `Hdf5Service::flush()` immediately before `writeExperimentInfo()` to reduce
  risk of metadata writes invalidating already-persisted frame batches.
- `onUpdateStats` — timer tick; pulls `CaptureStats`, count-only
  `ProcessingService::getBufferedFrameCounts()`, and autofocus state for the
  status bar/sidebar. It must not copy full `ProcessedFrame` buffers on the
  500 ms UI timer.
- `onTabChanged(index)` — starts/stops the realtime loop when entering or
  leaving the experiment-related tabs (ExperimentController state).
- `onNoCamerasFound` — shows a friendly dialog when
  `DeviceInitManager` reports empty discovery.

- EGrabber JavaScript camera scripts are skipped during `onTabChanged` when
  a MindVision camera is selected.

## Composition

- `connectTab_`, `overviewTab_`, `experimentTabs_` (QTabWidget with child
  tabs), `playbackPanel_`, `sidebarWidget_`, `updater_`, `initManager_`.
- `QFutureWatcher<size_t> flushWatcher_` — used to await the final HDF5
  flush on experiment stop without blocking the UI thread.
- The nested Experiment pages wire Monitoring apply into
  `AppConfigWatcher::writeBackProcessingConfig()`, which now emits a direct
  config-change signal after persistence so Preview and Monitoring refresh
  without waiting for a filesystem watcher round-trip.

## Boot disable GUI

- Settings menu includes **Boot Service Toggles...** for selecting services to
  disable on next launch.
- Selection is persisted in `QSettings` at `Startup/DisabledServices`.
- `main.cpp` applies this persisted CSV to `MIB_DISABLED_SERVICES` before
  backend initialization (unless the env var is already explicitly set).
- `auto_update` is honored in `MainWindow` startup: updater construction and
  quiet update checks are skipped when disabled.
- `main.cpp` now clamps startup window geometry to the active screen's
  `availableGeometry()` before `show()`, preventing initial windows from
  opening off-screen on smaller displays.

## Gotchas

- `closeEvent` must stop capture + experiment cleanly. Mis-ordering causes
  the stale `StreamModule` stats seen in `docs/howto/safe-start-stop-egrabber.md`.
- `experimentServicesActive_` must stay in sync with
  `ExperimentController::State` or buttons wedge.
