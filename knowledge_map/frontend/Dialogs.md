# Dialogs

> Short-lived modal widgets. One note for all — each is small.

**Source:** `src/frontend/dialogs/`, `include/frontend/dialogs/`

| Dialog | Purpose | Surfaced by |
|---|---|---|
| `MockConfigDialog` | Pick mock camera folder, interval, loop | [[ConnectTab]] |
| `ProcessingSettingsDialog` | Edit `ProcessingConfig` (full form) | [[ConfigTabs]] / menu |
| `MonitoringSettingsDialog` | Chart bin counts, axis ranges, refresh rate | [[ExperimentMonitoringTab]] |
| `BufferSaveDialog` | Save FrameStore frames to disk. Output Format group chooses single uncompressed AVI file (default) or TIFF folder (one `frame_NNNNNN.tiff` per frame). AVI mode swaps the browse button to a `getSaveFileName` flow with `.avi` filter and surfaces an FPS spinner (30 default, playback metadata only). On save, the output path auto-iterates with `_1`, `_2`, ... if the destination already exists (files or non-empty directories), so the dialog never overwrites. After a successful AVI save the confirmation dialog mentions that ImageJ/Fiji can open the file (no launcher — the user opens it themselves). Range selection (all/index/timestamp) and empty-frame filter apply to both formats. | [[PreviewPage]] |
| `ConversionFactorDialog` | Set pixel→μm conversion factor | [[PreviewPage]] |
| `FrameViewerDialog` | Popout frame inspector with overlay toggles | [[HdfReviewTab]], [[PreviewPage]] |
| `SyringePumpSettingsDialog` | Per-pump serial port + syringe volume defaults for the first two pumps (Sample/Sheath in the main app) | [[SyringePumpTab]] |
| `BatchMaskDialog` | Re-generate masks from HDF5 range, image folder, or AVI file via [[../services/ProcessingService]]'s `processBatch`. Two-panel layout: controls on left, preview canvas on right. Uses `RoiDrawCanvas` for drag-to-draw ROI selection; ROI pre-populated from HDF5 `experiment_info`. Frame nav buttons (←/→) lazy-load one frame at a time for background selection. AVI source uses a cached `cv::VideoCapture` for preview seeks. Overrides live pipeline ROI/background with dialog-selected values. | [[HdfReviewTab]] |
| `RoiDrawCanvas` (util widget) | Displays a `QImage` scaled to fit and lets the user drag a rectangle to define an ROI in image coordinates. Emits `roiChanged(QRect)` on release. Owned by `BatchMaskDialog`. Source: `src/frontend/utils/RoiDrawCanvas.cpp` | `BatchMaskDialog` |

## Conventions

- Each dialog takes a non-owning reference to the relevant service or
  config struct and emits/returns an updated value on `accept()`.
- Dialogs do **not** spawn threads or open serial ports themselves — they
  only mutate config; opening happens in the owning tab.
