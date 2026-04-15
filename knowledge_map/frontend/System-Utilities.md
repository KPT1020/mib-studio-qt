# System & Utilities

> Non-tab helpers used across the frontend.

**Source:** `src/frontend/system/`, `src/frontend/utils/`,
`src/frontend/widgets/`, `src/frontend/models/`

## System (`src/frontend/system/`)

- **`AppConfigWatcher`** — `QFileSystemWatcher` over an external JSON
  config. Reloads config and propagates to
  [[../services/ProcessingService]] / [[ConfigTabs]] on change. See
  `docs/howto/live-config-reload.md`.
- **`AutoUpdater`** — periodic update check; see
  `docs/howto/auto-update-rustfs.md` and `docs/howto/release-workflow.md`.
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

## Widgets (`src/frontend/widgets/`)

- **`ZoomableChartView`** — subclass of `QChartView` with scroll/zoom.
  Used by [[ExperimentMonitoringTab]] and [[HdfReviewTab]].

## Cross-thread Qt signals

- **`BackgroundCaptureNotifier`** (`src/backend/BackgroundCaptureNotifier.cpp`)
  — tiny `QObject` that bridges non-Qt thread callbacks (e.g.
  background auto-capture from [[../services/ProcessingService]]) to Qt
  signals on the main thread. Owned by [[../architecture/AppBackend]].
