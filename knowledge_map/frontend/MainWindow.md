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
  risk of metadata writes invalidating already-persisted frame batches. When
  multi-image is enabled and realtime mode is `async_batch`, start now
  temporarily switches processing to `inline` for the experiment so series
  frames are actually captured and reviewable; stop restores the prior mode.
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

## Menu bar

- **File:** Open Data Folder (`Documents/MIB_Studio_Qt`), Open Logs Folder
  (`%LOCALAPPDATA%/MIB_Studio_Qt/logs`), Exit.
- **Settings:** Processing / Monitoring / Pixel-to-Micron / Syringe Pump
  settings, Boot Service Toggles (added in code), and **Profiles…** (navigates
  to Experiment ▸ Preview, which hosts the config/profiles editor).
- **Help:** About, **Software Updates…** (opens [[System-Utilities]]'s
  `SoftwareUpdatesDialog` — channel + version selection; replaced the old
  "Check for Updates…"), Documentation and Report a Problem (open the GitHub
  repo/issues). The Software Updates action is disabled when `auto_update` is
  disabled at boot.

## Boot disable GUI

- Settings menu includes **Boot Service Toggles...** for selecting services to
  disable on next launch.
- Selection is persisted in `QSettings` at `Startup/DisabledServices`.
- `main.cpp` applies this persisted CSV to `MIB_DISABLED_SERVICES` before
  backend initialization (unless the env var is already explicitly set).
- `auto_update` is honored in `MainWindow` startup: updater construction and
  quiet update checks are skipped when disabled.

## Gotchas

- `closeEvent` must stop capture + experiment cleanly. Mis-ordering causes
  the stale `StreamModule` stats seen in `docs/howto/safe-start-stop-egrabber.md`.
- `experimentServicesActive_` must stay in sync with
  `ExperimentController::State` or buttons wedge.
