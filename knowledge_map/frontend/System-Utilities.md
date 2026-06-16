# System & Utilities

> Non-tab helpers used across the frontend.

**Source:** `src/frontend/system/`, `src/frontend/utils/`,
`src/frontend/widgets/`, `src/frontend/models/`

## System (`src/frontend/system/`)

- **`AppConfigWatcher`** — `QFileSystemWatcher` over an external JSON
  config. Reloads config and propagates to
  [[../services/ProcessingService]] / [[ConfigTabs]] on change. Monitoring
  apply now writes the full supported `ProcessingConfig` back to the active
  config file and emits `configFileChanged` immediately so Preview and
  Monitoring refresh without waiting for watcher feedback. See
  `docs/howto/live-config-reload.md`.
- **`AutoUpdater`** — periodic update check; see
  `docs/howto/auto-update-r2.md` and `docs/howto/release-workflow.md`.
- **`ProfileManager`** — profile catalog/metadata helper for
  `ConfigTabs`. Scans local profiles, lazily generates `profile.meta.json`,
  parses public R2 catalogs, computes SHA-256, stages/installs updates, and
  produces field-level JSON diffs for manual update review.
- **`DeviceInitManager`** — runs [[../services/CameraControlService]]
  `discoverCameras()` off the UI thread. Emits a signal when discovery
  completes (including "no cameras found").
- **`PlaybackPanel`** — the scrub+preview widget used by [[PreviewPage]]
  and [[MainWindow]]. Owns a `QImage` display, ROI overlay, scrub slider,
  display-FPS throttle, and overlay mode (Off/Mask/Contours/Both).

## Models (`src/frontend/models/`)

- **`JsonTableModel`** — table view of flattened JSON (used by
  [[ConfigTabs]]).
- **`HdfMetricsModel`** — `QAbstractTableModel` reading metadata from
  [[../services/Hdf5Service]] (`readValidMetadata`, `readInvalidMetadata`).

## Utils (`src/frontend/utils/`)

`BackgroundPreviewWidget`, `ConfigPathManager`, `EgrabberConfigParser`,
`FileIOUtils`, `JsonFlatten`, `OverlayRenderer`, `RoiManager`,
`SidebarWidget`, `SimpleImageCanvas`, `StatisticsPanel`,
`StatsDisplayManager`.

- **`StatisticsPanel`** — left-column telemetry groups for display, processing,
  camera, autofocus, and experiment state. Experiment telemetry includes the
  selected HDF5 save location plus live free/total storage space for the
  underlying device/volume.

## Widgets (`src/frontend/widgets/`)

- **`ZoomableChartView`** — subclass of `QChartView` with scroll/zoom.
  Used by [[ExperimentMonitoringTab]] and [[HdfReviewTab]].

## Gotchas

- `AppConfigWatcher` is still the source of truth for live config reloads,
  but programmatic write-back now emits a direct change notification after
  persisting the file so sibling tabs do not depend on filesystem watcher
  timing.
- Profile catalog checks are manual only. `ConfigTabs` now owns the user
  interaction, while `ProfileManager` handles the file/network/hash work and
  keeps local metadata synchronized with updated local profiles.

## Cross-thread Qt signals

- **`BackgroundCaptureNotifier`** (`src/backend/BackgroundCaptureNotifier.cpp`)
  — tiny `QObject` that bridges non-Qt thread callbacks (e.g.
  background auto-capture from [[../services/ProcessingService]]) to Qt
  signals on the main thread. Owned by [[../architecture/AppBackend]].
