# HDF5 Storage

> Schema and patterns for experiment files. Implementation is behind a
> PIMPL in [[../services/Hdf5Service]].

**Source:** `src/backend/recording/Hdf5Service.cpp`
**Related:** [[../services/Hdf5Service]], [[../services/ProcessingService]]
(`ProcessedFrame`, `ProcessingConfig`), [[../frontend/HdfReviewTab]]

## File layout (conceptual)

- `/experiment_info` — root attributes:
  `startTimeNs`, `endTimeNs`, `totalValidFrames`, `totalInvalidFrames`,
  serialized `ProcessingConfig`, ROI, optional `background` image,
  `config_json` (raw JSON string), `readiness_json` (UX-6 pre-start
  readiness snapshot + override record; absent on legacy files —
  `readConfigJson`/`readReadinessJson` return false rather than erroring),
  plus processing-core provenance:
  `processing_core_version`, `processing_contract_version`,
  `processing_engine_abi_version`, `processing_core_sha256`,
  `processing_manifest_sha256`, `processing_release_tag`,
  `processing_core_source`, `processing_core_build_id`, and
  `processing_runtime_fingerprint`.
- `/valid_frames/` — per-field datasets (images, masks, metrics).
- `/invalid_frames/` — same shape; populated only at
  `invalidFrameSamplingRate` sampling.
- `/series_images` — 4D `(N, seriesCount, H, W)` for multi-image mode.
- `/recorded_frames/` — used by frame-recording mode (images + basic
  metadata only; no contour metrics).
- `/recording_info` — raw-recording totals/config plus the same nine
  processing-core provenance attributes as `/experiment_info`.
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
  `writeExperimentInfo` receives the exact core identity used by the live or
  offline operation; `readProcessingCoreIdentity` returns it to review/tooling
  callers and reports absence for legacy files.
  Append steps flush on a time interval (`maybeIntervalFlush`); one-shot
  finalization writes (`writeExperimentInfo`, `writeConfigJson`) flush
  unconditionally.
- **Recording mode**: `initializeRecordingDatasets()` →
  `appendRecordingFrames(images, metadata)` →
  `writeRecordingInfo(..., processingCore)`. The recorder holds one operation
  lease from start through final metadata so the identity describes the exact
  core used for every empty-frame decision.
  Recording appends also flush on the same time interval — no per-append
  full-file copy. Finalization performs a global HDF5 flush and strong
  `H5Fclose`, so the superblock EOA is updated before `stopFrameRecording()`
  returns.
- **Interval flush**: append paths call `H5Fflush` at most once per
  `MIB_HDF5_FLUSH_INTERVAL_MS` (default 5000 ms) so the recorder thread stays
  off synchronous I/O on every batch. A crash loses at most one interval's
  worth of buffered frames; there is no recovery sidecar.

## Read paths (scalable)

- Metadata only: `readValidMetadata`, `readInvalidMetadata`. Use these to
  populate `HdfMetricsModel` without loading image bytes.
- Recording mode: `isRecordingFile()` probes `/recording_info`;
  `readRecordingMetadata` fills minimal `ProcessedFrame`s (index +
  timestampNs only); `readRecordingInfo` reads the core attributes
  (`start_time_ns`, `end_time_ns`, `total_recorded_frames`,
  `total_filtered_empty_frames`) plus optional multi-image attributes
  (`multi_image_enabled`, `multi_image_count`). [[../frontend/HdfReviewTab]]
  uses these to present recording files in a single-tab view and build
  recording-series windows in FrameViewer when multi-image was enabled.
- Single image (bounded memory): `readImageByIndex(datasetPath, idx, cv::Mat&)`.
- Small batch (e.g. thumbnails): `readImagesRange(datasetPath, start, count, vec)`.
- Dataset shape discovery: `getDatasetInfo(path, count, H, W, channels)`;
  `getSeriesImageInfo(count, seriesCount, H, W)`.

## Gotchas

- `writeConfigJson` **must** be called after `writeExperimentInfo` — see
  header docstring.
- There is no recovery sidecar: if the primary file fails to open,
  `Hdf5Service::loadFile` fails directly. A crash/power-loss mid-recording can
  lose up to one flush interval and may leave the primary `.h5` needing
  `h5clear --increment` (or unrecoverable) — the accepted tradeoff for
  real-time recorder throughput.
- A stale EOA/superblock that can be repaired with `h5clear --increment`
  points at an interrupted or failed final flush/close. Recorder logs include
  final flush status, close timing, and the HDF5 object count before close to
  make that diagnosable.
- Don't use `readValidFrames` (full load) on files > 1 GB; prefer the
  scalable hyperslab APIs. See task
  `knowledge_map/task/review_2gb_scalability.md`.
- 3D `(N,H,W)` and 4D `(N,H,W,C)` datasets both supported by
  `readImageByIndex` — channels auto-detected.
