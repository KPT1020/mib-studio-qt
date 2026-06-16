# Hdf5Service

> Batched HDF5 read/write for experiment frames + metadata, plus
> frame-recording mode and review-time scalable (hyperslab) reads.
> HDF5 headers are hidden behind a PIMPL.

**Source:** `src/backend/recording/Hdf5Service.cpp`,
`include/backend/recording/Hdf5Service.h`
**Related:** [[ProcessingService]], [[../data-model/HDF5-Storage]],
[[../frontend/HdfReviewTab]]

## Responsibility

- Opens one HDF5 file at a time (`openFile`, `loadFile`, `closeFile`).
- Writes valid/invalid frames as bulk (`saveFrames`) or incrementally
  (`initializeDatasets` + `appendFrames`).
- Stores experiment metadata: start/end time (ns), totals, `ProcessingConfig`,
  ROI, optional background image; plus raw config JSON via `writeConfigJson`.
- Forces an HDF5 flush after append/metadata writes and maintains a rolling
  checkpoint sidecar `<file>.recovery.h5`.
- Checkpoint copies are **throttled on append paths** (time/size gated) to
  avoid repeated full-file copies on high-throughput runs; metadata finalization
  (`writeExperimentInfo`, `writeConfigJson`, `writeRecordingInfo`) still forces
  an immediate checkpoint refresh.
- Review reads: `readValidFrames`, `readInvalidFrames`, plus scalable
  `readImageByIndex` / `readImagesRange` using hyperslabs.
- Metadata-only reads (`readValidMetadata` / `readInvalidMetadata`) skip
  image payloads — used by [[../frontend/HdfReviewTab]] for lazy virtualization.
- Chart snapshots: `saveChartSnapshot` / `readChartSnapshot` (2D/3D images
  without batch dim).
- **Multi-image series** (`multi_image_enabled` in ProcessingConfig):
  `getSeriesImageInfo`, `readSeriesImagesByIndex` — 4D `(N, seriesCount, H, W)`.
  The write path packs each frame's full series into one HDF5 write call
  (instead of per-image writes) to reduce stop/flush latency in high
  `multi_image_count` runs.
- **Frame recording mode** (raw frames, no contours):
  `initializeRecordingDatasets`, `appendRecordingFrames`, `writeRecordingInfo`
  with `RecordingFrameMeta` (index, timestampNs, width, height).
  Matching readers: `isRecordingFile()` (probes `/recording_info`),
  `readRecordingMetadata(frames)` (fills only index + timestampNs on each
  `ProcessedFrame`), `readRecordingInfo(start, end, total, filtered, ...)`.
  Recording info now persists `multi_image_enabled` (`uint8`) and
  `multi_image_count` (`uint64`) alongside start/end/totals so review mode can
  reconstruct multi-image windows from `/recorded_frames/images`.
  Used by [[../frontend/HdfReviewTab]] to present recording files.

## Threading

Blocking I/O on whichever thread calls it. In practice:
- Experiment flush: called from `ProcessingService::flushBufferedFrames`.
- Review reads: called from Qt main thread via [[../frontend/HdfReviewTab]].
- Frame recording writes: called from `AppBackend`'s recording thread.

## Gotchas

- `writeConfigJson` must follow `writeExperimentInfo`.
- `loadFile(path)` now auto-falls back to `path + ".recovery.h5"` if the
  primary file cannot be opened (e.g. interrupted write/corruption).
- A checkpoint may intentionally lag behind the latest append because append
  checkpoints are throttled; forced checkpoints occur at metadata/finalization.
- Scalable reads (`readImageByIndex`, `readImagesRange`) support both 3D
  `(N,H,W)` and 4D `(N,H,W,C)` datasets — use them for large files instead
  of `readValidFrames` to avoid OOM. See task
  `knowledge_map/task/review_2gb_scalability.md`.
- PIMPL means you can't see HDF5 types in headers — look at
  `src/backend/recording/Hdf5Service.cpp` for dataset paths and dtypes.
