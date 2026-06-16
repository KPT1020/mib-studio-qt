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
- `Export All` now prompts how to export multi-image series frames:
  all frames, a custom 1-based range (for example `9-15`), or skip.

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
- `readRecordingInfo` also exposes persisted multi-image flags
  (`multi_image_enabled`, `multi_image_count`). When enabled (`count > 1`),
  `showFrameViewer` synthesizes a per-frame series window by reading a bounded
  range from `/recorded_frames/images`, so FrameViewer's series prev/next
  controls are available in recording review mode.
- Dataset reads go through `imagesPath(bool)` / `masksPath(bool)` helpers
  that route to `/recorded_frames/images` (and return `""` for masks,
  since recording files have none).
- Disabled in recording mode: overlay combo, ROI overlay, Export Metrics
  (no per-frame metrics), Export Charts (no metrics to chart). Export All
  still writes the raw TIFF images. Regenerate Masks remains enabled and
  feeds `/recorded_frames/images` into `BatchMaskDialog`, then reloads the
  standard remasked HDF5 output.
- `clearDisplay()` restores default tab labels/visibility so loading an
  experiment file after a recording file works correctly.

## Regenerate masks button

Toolbar action **"Regenerate masks…"** opens [[Dialogs|BatchMaskDialog]]
(`include/frontend/dialogs/BatchMaskDialog.h`). The dialog drives
[[../services/ProcessingService]]'s `processBatch` API on either the
currently loaded HDF5 file's image dataset (`/valid_frames/images` for
standard experiment files, `/recorded_frames/images` for recording files)
with start/count, the whole HDF5 file, an AVI file, or a folder of
TIFF/PNG/JPEG images. Whole-file mode processes all recording frames for
recording files and both valid + invalid datasets for standard review files.
HDF5-sourced runs preserve source frame indices and store timestamps
normalised to the first regenerated image. Output is saved to a new HDF5
file via [[../services/BatchMaskSources]] `saveMasksToHdf5` and the tab
reloads from that file. If the user does not select a background frame,
the dialog can synthesize one tile-by-tile from the least-changing source
frames. The currently active
`ProcessingConfig`, ROI, and background image (from
`processing().getProcessingConfig()` / `getRealtimeRoi()` /
`getRealtimeBackgroundGray()`) are used as inputs.

## Gotchas

- Multi-image series (4D dataset) need `readSeriesImagesByIndex` — not the
  flat `readImageByIndex`.
- During `Export All`, the series prompt applies one range selection across
  every valid multi-image record so exports stay consistent.
- Chart snapshots stored in the file are 2D/3D — use `readChartSnapshot`
  to display them.
- See tasks `review_hdf_thumbnail_spacer_crash.md` and
  `fix_hdfreviewtab_linker_error.md` for historical fixes.
