# System & Utilities

> Non-tab helpers used across the frontend.

**Source:** `src/frontend/system/`, `src/frontend/utils/`,
`src/frontend/widgets/`, `src/frontend/models/`

## System (`src/frontend/system/`)

- **`LutHttpFetcher`** — `mib::frontend::makeQtLutHttpGet()` returns a
  `backend::HttpGetFn` (a blocking QtNetwork GET: single-shot event loop +
  transfer timeout). `main.cpp` injects it via `AppBackend::setLutHttpFetcher`
  so the Qt-free backend catalog ([[../architecture/AppBackend]] LUT
  management, ADR 0002) can fetch the E-modulus LUT without linking
  `Qt6::Network`. A Tauri/Rust shell supplies its own fetcher through the same
  seam.
- **`AppConfigWatcher`** — `QFileSystemWatcher` over an external JSON
  config. Reloads config and propagates to
  [[../services/ProcessingService]] / [[ConfigTabs]] on change. Monitoring
  apply now writes the full supported `ProcessingConfig` back to the active
  config file and emits `configFileChanged` immediately so Preview and
  Monitoring refresh without waiting for watcher feedback. See
  `docs/howto/live-config-reload.md`.
  On startup, when the app-managed `config.json` already exists,
  `mergeNewDefaultsIntoConfig` deep-merges any keys added to the bundled
  `:/defaults/config.json` by a newer build into the existing file
  (`frontend::jsonutil::mergeMissingDefaults`, in `JsonConfigMerge`),
  **preserving all existing user values**. This stops an updated install from
  drifting away from a fresh install when new config keys are introduced. An
  external user-chosen config path is never rewritten.
- **`AutoUpdater`** — update check + channel/version selection; see
  `docs/howto/auto-update-r2.md` and `docs/howto/release-workflow.md`.
  - Channel persisted in `QSettings` (`Update/Channel`, `stable`|`beta`); the
    default is the **build's own channel**, derived from its full version via
    `UpdateCatalog::channelForVersion` (a `-beta.` suffix → beta), so a beta
    build opens on the beta channel and marks its own release "current". An
    explicit user choice still wins; `MIB_STUDIO_UPDATE_MANIFEST_URL` env
    override still wins over everything. `defaultManifestUrl` is channel-aware
    (`{channel}/latest.json`).
  - **Build version identity:** `applicationVersion` is set from
    `MIB_STUDIO_QT_VERSION_FULL` (the git tag *with* its `-beta.N` suffix), not
    the stripped `MIB_STUDIO_QT_VERSION`. Without the suffix a stable and a beta
    build were byte-identical in version, so the app could not tell which channel
    it was on (both showed the same "current version"). `MIBVersion.cmake`
    exposes both: `PROJECT_VERSION` (numeric, for `project()`/installers) and
    `PROJECT_VERSION_FULL` (with suffix, for runtime identity + Sentry release).
  - `fetchVersionIndex()` GETs `{channel}/index.json` and emits
    `versionIndexReady`/`versionIndexFailed`, parsed by `UpdateCatalog`.
  - `installVersion(entry)` maps a catalog `VersionEntry` onto the existing
    download → SHA-256 verify → elevated-install path. The silent startup
    auto-check (latest.json) is unchanged.
  - Driven by **`SoftwareUpdatesDialog`** (`dialogs/`), opened from **Help ▸
    Software Updates…**: channel dropdown + version list with rollback (a
    downgrade is confirmed via `UpdateCatalog::isDowngrade`).
- **`ProfileManager`** — profile catalog/metadata helper for
  `ConfigTabs`. Scans local profiles, lazily generates `profile.meta.json`,
  parses public R2 catalogs, computes SHA-256, stages/installs updates, and
  produces field-level JSON diffs for manual update review. An optional
  `processing_contract_version` is round-tripped through catalog/local
  metadata and marks the profile incompatible when it differs from the active
  core; it never selects a core.
- **`DeviceInitManager`** — runs [[../services/CameraControlService]]
  `discoverCameras()` off the UI thread. Emits a signal when discovery
  completes (including "no cameras found").
- **`PlaybackPanel`** — the scrub+preview widget used by [[PreviewPage]]
  and [[MainWindow]]. Owns a `QImage` display, ROI overlay, scrub slider,
  display-FPS throttle, and overlay mode (Off/Mask/Contours/Both).
  - Overlay cell color (blue=target / green=valid / red=invalid) only uses
    the live [[../services/ProcessingService]] `getLatestSnapshot()` while
    *following live*. When stopped/scrubbing/replaying buffered frames the
    snapshot is stale and unrelated to the on-screen frame, so the color is
    re-derived per displayed frame via `computeProcessedFrame()` — otherwise
    the cell stays stuck on the last live frame's color (usually red).

## Models (`src/frontend/models/`)

- **`JsonTableModel`** — table view of flattened JSON (used by
  [[ConfigTabs]]).
- **`HdfMetricsModel`** — `QAbstractTableModel` reading metadata from
  [[../services/Hdf5Service]] (`readValidMetadata`, `readInvalidMetadata`).

## Utils (`src/frontend/utils/`)

`BackgroundPreviewWidget`, `ConfigPathManager`, `EgrabberConfigParser`,
`FileIOUtils`, `JsonFlatten`, `JsonConfigMerge` (pure deep-merge of bundled
defaults into an existing `config.json`; tested by
`tests/frontend/json_config_merge_test.cpp`), `UpdateCatalog` (pure parse/sort
of a channel `index.json` into a newest-first version list + downgrade check;
tested by `tests/frontend/update_catalog_test.cpp`), `OverlayRenderer`,
`RoiManager`, `SidebarWidget`, `SimpleImageCanvas`, `StatisticsPanel`,
`StatsDisplayManager`.

- **`ApplicationSettings`** — establishes the stable `MIB Studio` /
  `MIB Studio Qt` identity before any default `QSettings` access. Its one-time,
  versioned migration copies every missing user key from the former
  `Unknown Organization` namespace, never overwrites stable values or deletes
  the legacy store, and marks completion only after a successful sync. Desktop
  startup fails closed if this cannot be written.
- **`ProcessingCoreSettings`** — synchronizes the complete explicit native-core
  selection as one pre-activation commit. It snapshots and restores the prior
  logical values if `QSettings::sync()` fails; the backend does not swap the
  kernel unless this helper returns success.

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
