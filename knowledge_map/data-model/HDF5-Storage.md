# HDF5 Storage

> Schema and patterns for experiment files. Implementation is behind a
> PIMPL in [[../services/Hdf5Service]].

**Source:** `src/backend/services/Hdf5Service.cpp`
**Related:** [[../services/Hdf5Service]], [[../services/ProcessingService]]
(`ProcessedFrame`, `ProcessingConfig`), [[../frontend/HdfReviewTab]]

## File layout (conceptual)

- `/experiment_info` — root attributes:
  `startTimeNs`, `endTimeNs`, `totalValidFrames`, `totalInvalidFrames`,
  serialized `ProcessingConfig`, ROI, optional `background` image,
  `config_json` (raw JSON string).
- `/valid_frames/` — per-field datasets (images, masks, metrics).
- `/invalid_frames/` — same shape; populated only at
  `invalidFrameSamplingRate` sampling.
- `/series_images` — 4D `(N, seriesCount, H, W)` for multi-image mode.
- `/recorded_frames/` — used by frame-recording mode (images + basic
  metadata only; no contour metrics).
- Chart snapshot datasets — 2D/3D `cv::Mat` saved via
  `saveChartSnapshot(path, image)`.

## Write paths

- **Batch save** (rarely used for experiments, useful for tests):
  `saveFrames(valid, invalid)`.
- **Incremental** (primary path):
  `initializeDatasets()` →
  `appendFrames(valid, invalid)` repeatedly →
  `writeExperimentInfo(...)` →
  optional `writeConfigJson(...)`.
  Called from `ProcessingService::flushBufferedFrames`.
- **Recording mode**: `initializeRecordingDatasets()` →
  `appendRecordingFrames(images, metadata)` →
  `writeRecordingInfo(...)`.

## Read paths (scalable)

- Metadata only: `readValidMetadata`, `readInvalidMetadata`. Use these to
  populate `HdfMetricsModel` without loading image bytes.
- Single image (bounded memory): `readImageByIndex(datasetPath, idx, cv::Mat&)`.
- Small batch (e.g. thumbnails): `readImagesRange(datasetPath, start, count, vec)`.
- Dataset shape discovery: `getDatasetInfo(path, count, H, W, channels)`;
  `getSeriesImageInfo(count, seriesCount, H, W)`.

## Gotchas

- `writeConfigJson` **must** be called after `writeExperimentInfo` — see
  header docstring.
- Don't use `readValidFrames` (full load) on files > 1 GB; prefer the
  scalable hyperslab APIs. See task
  `knowledge_map/task/review_2gb_scalability.md`.
- 3D `(N,H,W)` and 4D `(N,H,W,C)` datasets both supported by
  `readImageByIndex` — channels auto-detected.
