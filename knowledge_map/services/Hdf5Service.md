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
- Append hot paths (`appendFrames`, `appendRecordingFrames`) flush via
  `maybeIntervalFlush()`: an `H5Fflush(H5F_SCOPE_GLOBAL)` at most once per
  `MIB_HDF5_FLUSH_INTERVAL_MS` (default 5000 ms). This keeps the recorder
  thread off synchronous I/O on every batch, so it cannot fall behind the
  `frameStore_` ring buffer and drop frames. One-shot finalization writes
  (`writeExperimentInfo`, `writeConfigJson`, `writeRecordingInfo`) flush
  unconditionally. There is **no** `.recovery.h5` sidecar copy.
- Writable files are created with HDF5 strong-close semantics and
  `closeFile()` performs an explicit final global flush before `H5Fclose`.
  This protects the superblock/EOA state after recording stop and logs final
  flush status, close timing, and the open-object count. A crash mid-recording
  can lose up to one flush interval — the accepted tradeoff for throughput.
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
  Matching readers: `isRecordingFile()` (probes `/recording_info`),
  `readRecordingMetadata(frames)` (fills only index + timestampNs on each
  `ProcessedFrame`), `readRecordingInfo(start, end, total, filtered)`.
  Used by [[../frontend/HdfReviewTab]] to present recording files.

## Threading

Blocking I/O on whichever thread calls it. In practice:
- Experiment flush: called from `ProcessingService::flushBufferedFrames`.
- Review reads: called from Qt main thread via [[../frontend/HdfReviewTab]].
- Frame recording writes: called from `AppBackend`'s recording thread.

## Gotchas

- `writeConfigJson` must follow `writeExperimentInfo`.
- `loadFile(path)` opens the primary file only — there is no recovery-sidecar
  fallback. A corrupt/unfinalized primary fails to load (try
  `h5clear --increment`).
- Scalable reads (`readImageByIndex`, `readImagesRange`) support both 3D
  `(N,H,W)` and 4D `(N,H,W,C)` datasets — use them for large files instead
  of `readValidFrames` to avoid OOM. See task
  `knowledge_map/task/review_2gb_scalability.md`.
- PIMPL means you can't see HDF5 types in headers — look at
  `src/backend/recording/Hdf5Service.cpp` for dataset paths and dtypes.
- If a primary `.h5` opens only after `h5clear --increment`, check recorder
  logs for final flush/close errors and whether the app or host was killed
  before `stopFrameRecording()` returned. With interval flushing, a kill
  between flushes can leave up to `MIB_HDF5_FLUSH_INTERVAL_MS` of frames
  unflushed.
