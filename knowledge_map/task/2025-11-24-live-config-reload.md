Title: Live reload app config and propagate to services

Scope
- Watch active `config.json` (external if set via `Config/ExternalAppConfigPath`, else `../include/config.json`).
- On save/modify, reload and apply:
  - Processing pipeline params (`image_processing.*`) to `ProcessingService`.
  - Buffer flush threshold (`buffer_threshold`) to `ProcessingService::setFlushInterval`.
  - Display FPS (`display_fps`) to `PlaybackPanel`.
  - Autofocus config keys to `AutofocusService`.

Implementation
- Added `frontend::AppConfigWatcher` (QFileSystemWatcher) and wired it in `PreviewPage`.
- `ConfigTabs` emits `appConfigPathChanged` on Browse/Clear for retargeting the watcher.
- `ProcessingService::realtimeLoop` now uses config for blur, threshold, morph (ensures parity).
- `PlaybackPanel::computeProcessedOverlay` now uses `ProcessingService::getProcessingConfig()` for parity and exposes `setDisplayFps(int)`.

Notes
- Logging via spdlog per workspace rules.
- Defaults seeded from `:/defaults/config.json` when include path missing.
- Monitoring tab (valid/invalid frames, ring ratio histogram) automatically reflects updated classification from backend.


