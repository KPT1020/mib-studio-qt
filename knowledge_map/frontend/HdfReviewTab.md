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

## Gotchas

- Multi-image series (4D dataset) need `readSeriesImagesByIndex` — not the
  flat `readImageByIndex`.
- Chart snapshots stored in the file are 2D/3D — use `readChartSnapshot`
  to display them.
- See tasks `review_hdf_thumbnail_spacer_crash.md` and
  `fix_hdfreviewtab_linker_error.md` for historical fixes.
