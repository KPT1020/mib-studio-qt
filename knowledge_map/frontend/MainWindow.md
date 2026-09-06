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

- `onStartCapture` / `onStopCapture` — thin wrappers over the owned
  [[Controllers]] `CameraController` (`cameraController()` accessor). The
  controller's operation guard reads `experimentActive_`, `flushInProgress_`
  and `AppBackend::isFrameRecording()`; its `commandFailed` signal shows the
  warning dialog + status text, and `onCameraStateChanged` arms/disarms
  `statsTimer_` for every route (the sampling/flush scheduling in
  `onUpdateStats` no longer depends on which button started the camera) and
  writes the phase/failure text to the status label. The Preview page's
  `PlaybackPanel::captureToggleRequested` (Space) is wired to
  `requestToggle()` (issue #360).
- `onStartExperiment` / `onStopExperiment` — drive
  [[Controllers]] `ExperimentController`. Start now blocks with an explicit
  acknowledgement when `CaptureService::activeDeliveryMode()` is
  `LatestFrame`: "Switch to Every Frame" (default; routes through
  `ConnectTab::setDeliveryMode`, i.e. the same setConfig + persist path as
  the combo, without restarting the running capture), "Continue with Latest
  Frame" (explicit override), or Cancel (abort). Stop path now issues an explicit
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
- Startup restores the persisted native core through
  [[ProcessingCoreDialog]] before capture begins. A restore/pin failure is
  visible in the status bar and experiment start remains blocked.

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
  settings, **Processing Core…** ([[ProcessingCoreDialog]]), Boot Service
  Toggles (added in code), and **Profiles…** (navigates to Experiment ▸
  Preview, which hosts the config/profiles editor).
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

- `closeEvent` stops experiment services, then stops the capture service
  before the window destructs. Mis-ordering causes the stale `StreamModule`
  stats seen in `docs/howto/safe-start-stop-egrabber.md`. This and the
  `applyCameraScriptFromFile`/`applyMindVisionConfigFromFile` internal stops
  are the only camera stops that do not go through `CameraController`; both
  are documented lifecycle transactions guarded by the experiment check
  above, not user commands.
- The corner **Start Live View / Stop Camera** buttons are pure
  presentations of `cameraController()->startAction()/stopAction()`
  (object names `startCameraBtn`/`stopCameraBtn`); do not attach separate
  guard logic to them.
- The destructor stops `statsTimer_` before `delete ui` to prevent timer
  callbacks from accessing backend_ on a partially-destroyed widget tree.
- `experimentServicesActive_` must stay in sync with
  `ExperimentController::State` or buttons wedge.
- The status-bar `Core: <version> · contract <n>` label is authoritative for
  the selected engine. Experiment metadata receives that core identity on
  stop; activation itself is rejected while an operation is active.
- `deliveryModeLabel_` is a permanent status-bar badge ("▶ EVERY FRAME ·
  sequence preserved" / "⏩ LATEST FRAME · drops stale frames"), so the
  acquisition mode is visible on every tab. `updateDeliveryModeBadge()` shows
  the **backend-confirmed** `CaptureService::activeDeliveryMode()` and
  appends " (requested)" while `deliveryModeConfirmed` is false; it refreshes
  on every `onUpdateStats` tick and on combo/config changes.
  `CaptureService` clears the confirmed flag when the capture loop releases
  the camera, so between runs the badge tracks the requested config mode
  (with the "(requested)" suffix) — the ConnectTab combo is the
  requested-mode source of truth.
- The async experiment flush (`QtConcurrent::run` + `flushWatcher_`) captures
  the **backend pointer, not `this`**: `QFutureWatcher`'s destructor does not
  block on a running future, so the task can outlive the window (the backend
  cannot — it is constructed before the window in `main()`). The destructor
  additionally `waitForFinished()`s any in-flight flush before members die.
