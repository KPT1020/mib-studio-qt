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
- `onStartExperiment` now runs the **readiness gate** (UX-6): it collects
  `ReadinessFacts` (preflight facts, workflow confirmations, ROI,
  background, calibration, profile dirty/applied/verified, last-save
  health) and shows `ReadinessDialog`; a cancelled gate aborts the start.
  The accepted snapshot + overrides serialize via
  `checks::readinessToJson` into `pendingReadinessJson_`, written to the
  HDF5 file as `readiness_json` right after `writeConfigJson` at stop.
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
- Startup restores the persisted native core through
  [[ProcessingCoreDialog]] before capture begins. A restore/pin failure is
  visible in the status bar and experiment start remains blocked.

- EGrabber JavaScript camera scripts are skipped during `onTabChanged` when
  a MindVision camera is selected.

## Composition

- `connectTab_`, `overviewTab_`, `experimentTabs_` (QTabWidget with child
  tabs), `hdfReviewTab_`, `playbackPanel_`, `sidebarWidget_`, `updater_`,
  `initManager_`, `workflowBar_` ([[WorkflowStageBar]]).
- **Guided workflow (UX-1):** a [[WorkflowStageBar]] sits above the tabs.
  `refreshWorkflowState()` collects backend facts (camera configured,
  discovery failed, core pin, capture running, ROI valid, experiment
  active/completed/save-ok, review file loaded), evaluates them through
  `backend.workflow()` ([[../services/WorkflowStateService]]), and
  re-renders the bar. Driven by a 500 ms `workflowTimer_` plus immediate
  refreshes from the capture/experiment slots. `onStopExperiment` records
  `experimentCompleted_` / `lastExperimentSaveOk_` (from the
  metadata/provenance write) for the Experiment/Review stage state.
  Visiting a tab only syncs the bar highlight — completion comes from
  facts + the bar's explicit confirm actions.
- **Preflight checklist (UX-3):** a [[ChecklistPanel]] in a "Hardware
  preflight" group box appended to the Connect tab, fed from the same
  refresh with [[../services/OperatorChecks]] `evaluatePreflight`.
  `probeStorage()` checks the Documents/MIB_Studio_Qt folder
  (writability + free GB) every ~5 s. `handlePreflightRecovery(id)`
  routes Fix… buttons (camera → auto-connect, core → Processing Core
  dialog, storage → open data folder, profile → Profiles surface).
  Profile status (UX-2) is cached from `ConfigTabs::profileStatusChanged`.
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

- `closeEvent` must stop capture + experiment cleanly. Mis-ordering causes
  the stale `StreamModule` stats seen in `docs/howto/safe-start-stop-egrabber.md`.
- `experimentServicesActive_` must stay in sync with
  `ExperimentController::State` or buttons wedge.
- The status-bar `Core: <version> · contract <n>` label is authoritative for
  the selected engine. Experiment metadata receives that core identity on
  stop; activation itself is rejected while an operation is active.
- The async experiment flush (`QtConcurrent::run` + `flushWatcher_`) captures
  the **backend pointer, not `this`**: `QFutureWatcher`'s destructor does not
  block on a running future, so the task can outlive the window (the backend
  cannot — it is constructed before the window in `main()`). The destructor
  additionally `waitForFinished()`s any in-flight flush before members die.
