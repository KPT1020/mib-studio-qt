# HdfReviewTab

> Post-experiment review. Opens a saved HDF5 file and lets the user
> browse valid/invalid frames with their metrics.

**Source:** `src/frontend/tabs/HdfReviewTab.cpp`,
`include/frontend/tabs/HdfReviewTab.h`
**Related:** [[../services/Hdf5Service]], [[../data-model/HDF5-Storage]],
`models/HdfMetricsModel.cpp`

## Responsibility

- Open HDF5 file via `Hdf5Service::loadFile`.
- Lazily read metadata (not images) with
  `readValidMetadata` / `readInvalidMetadata`.
- On scroll/selection, fetch image payloads by index using
  `readImageByIndex` / `readImagesRange` (hyperslab reads — bounded memory).
- Display metrics in a `QTableView` backed by `HdfMetricsModel`.
- Optional charts: scatter + histograms over the saved dataset.

## Scalability

- Virtualised — the tab never loads all images at once.
- Supports files > 2 GB; see task
  `knowledge_map/task/review_2gb_scalability.md`.
- Close File button releases the HDF5 handle cleanly (recent crash fix —
  see `git log` entry
  "fix: add Close File button and fix dangling pointer crash in review tab").

## Recording-mode files

When a file has `/recording_info` present, [[../services/Hdf5Service]]'s
`isRecordingFile()` returns true and the tab branches into a single-view
layout:

- The "Invalid Frames" tab is hidden and "Valid Frames" is relabelled to
  "Frames"; all recorded frames populate `validFrames_`.
- Metadata comes from `readRecordingMetadata` (only `index` + `timestampNs`
  populated); status text uses `readRecordingInfo`.
- Dataset reads go through `imagesPath(bool)` / `masksPath(bool)` helpers
  that route to `/recorded_frames/images` (and return `""` for masks,
  since recording files have none).
- Disabled in recording mode: overlay combo, ROI overlay, Regenerate
  Masks, Export Metrics (no per-frame metrics), Export Charts (no
  metrics to chart). Export All still writes the raw TIFF images.
- `clearDisplay()` restores default tab labels/visibility so loading an
  experiment file after a recording file works correctly.

## Regenerate masks button

Toolbar action **"Regenerate masks…"** opens [[Dialogs|BatchMaskDialog]]
(`include/frontend/dialogs/BatchMaskDialog.h`). The dialog drives
[[../services/ProcessingService]]'s `processBatch` API on either the
currently loaded HDF5 file's `/valid_frames/images` (with start/count) or
a folder of TIFF/PNG/JPEG images. Outputs can be written as PNG masks,
saved to a new HDF5 file via [[../services/BatchMaskSources]]
`saveMasksToHdf5`, and/or pushed back into the tab to refresh thumbnails
and metrics with the newly-computed masks. The currently active
`ProcessingConfig`, ROI, and background image (from
`processing().getProcessingConfig()` / `getRealtimeRoi()` /
`getRealtimeBackgroundGray()`) are used as inputs.

## Gotchas

- Multi-image series (4D dataset) need `readSeriesImagesByIndex` — not the
  flat `readImageByIndex`.
- Chart snapshots stored in the file are 2D/3D — use `readChartSnapshot`
  to display them.
- See tasks `review_hdf_thumbnail_spacer_crash.md` and
  `fix_hdfreviewtab_linker_error.md` for historical fixes.
