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
  [[Controllers]] `ExperimentController`.
- `onUpdateStats` — timer tick; pulls `CaptureStats`, `ProcessingStats`, and
  autofocus state for the status bar.
- `onTabChanged(index)` — starts/stops the realtime loop when entering or
  leaving the experiment-related tabs (ExperimentController state).
- `onNoCamerasFound` — shows a friendly dialog when
  `DeviceInitManager` reports empty discovery.

## Composition

- `connectTab_`, `overviewTab_`, `experimentTabs_` (QTabWidget with child
  tabs), `playbackPanel_`, `sidebarWidget_`, `updater_`, `initManager_`.
- `QFutureWatcher<size_t> flushWatcher_` — used to await the final HDF5
  flush on experiment stop without blocking the UI thread.

### Boot-time update toggle

- `MIB_DISABLED_SERVICES=auto_update` disables `AutoUpdater` creation at startup,
  disables the "Check for Updates" menu action, and skips the quiet
  startup update check.

## Gotchas

- `closeEvent` must stop capture + experiment cleanly. Mis-ordering causes
  the stale `StreamModule` stats seen in `docs/howto/safe-start-stop-egrabber.md`.
- `experimentServicesActive_` must stay in sync with
  `ExperimentController::State` or buttons wedge.
