# 2026-06-16 — Multi-image runtime save range

## Goal

Support runtime multi-image saving choices during experiment capture:

- save all `N` images per detection trigger, or
- save only a selected 1-based range (example `9-15` when `N=15`).

This is applied while frames are accumulated for HDF5, not as a post-export filter.

## Implementation summary

- Extended `ProcessingConfig` with:
  - `multi_image_save_all`
  - `multi_image_save_start`
  - `multi_image_save_end`
- Added Tune Params controls in `ExperimentMonitoringTab` under **Multi-Image**
  to edit those fields at runtime.
- Updated `AppConfigWatcher` to read/write the new keys in
  `image_processing.multi_image`.
- Updated realtime experiment accumulation in `ProcessingService::realtimeInlineLoop`
  to apply the configured save window to additional series frames.

## Behavior notes

- Trigger frame remains retained for metrics.
- Save range is applied to additional captured frames in the series.
- Range is clamped to `[1, multi_image_count]` and normalized so start <= end.

## Files touched

- `include/backend/processing/ProcessingService.h`
- `src/backend/processing/ProcessingService.cpp`
- `include/frontend/tabs/ExperimentMonitoringTab.h`
- `src/frontend/tabs/ExperimentMonitoringTab.cpp`
- `src/frontend/system/AppConfigWatcher.cpp`
- `resources/defaults/config.json`
