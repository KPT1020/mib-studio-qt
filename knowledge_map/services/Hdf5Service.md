# Hdf5Service

> Batched HDF5 read/write for experiment frames + metadata, plus
> frame-recording mode and review-time scalable (hyperslab) reads.
> HDF5 headers are hidden behind a PIMPL.

**Source:** `src/backend/services/Hdf5Service.cpp`,
`include/backend/services/Hdf5Service.h`
**Related:** [[ProcessingService]], [[../data-model/HDF5-Storage]],
[[../frontend/HdfReviewTab]]

## Responsibility

- Opens one HDF5 file at a time (`openFile`, `loadFile`, `closeFile`).
- Writes valid/invalid frames as bulk (`saveFrames`) or incrementally
  (`initializeDatasets` + `appendFrames`).
- Stores experiment metadata: start/end time (ns), totals, `ProcessingConfig`,
  ROI, optional background image; plus raw config JSON via `writeConfigJson`.
- Review reads: `readValidFrames`, `readInvalidFrames`, plus scalable
  `readImageByIndex` / `readImagesRange` using hyperslabs.
- Metadata-only reads (`readValidMetadata` / `readInvalidMetadata`) skip
  image payloads — used by [[../frontend/HdfReviewTab]] for lazy virtualization.
- Chart snapshots: `saveChartSnapshot` / `readChartSnapshot` (2D/3D images
  without batch dim).
- **Multi-image series** (`multi_image_enabled` in ProcessingConfig):
  `getSeriesImageInfo`, `readSeriesImagesByIndex` — 4D `(N, seriesCount, H, W)`.
- **Frame recording mode** (raw frames, no contours):
  `initializeRecordingDatasets`, `appendRecordingFrames`, `writeRecordingInfo`
  with `RecordingFrameMeta` (index, timestampNs, width, height).

## Threading

Blocking I/O on whichever thread calls it. In practice:
- Experiment flush: called from `ProcessingService::flushBufferedFrames`.
- Review reads: called from Qt main thread via [[../frontend/HdfReviewTab]].
- Frame recording writes: called from `AppBackend`'s recording thread.

## Gotchas

- `writeConfigJson` must follow `writeExperimentInfo`.
- Scalable reads (`readImageByIndex`, `readImagesRange`) support both 3D
  `(N,H,W)` and 4D `(N,H,W,C)` datasets — use them for large files instead
  of `readValidFrames` to avoid OOM. See task
  `knowledge_map/task/review_2gb_scalability.md`.
- PIMPL means you can't see HDF5 types in headers — look at
  `src/backend/services/Hdf5Service.cpp` for dataset paths and dtypes.
