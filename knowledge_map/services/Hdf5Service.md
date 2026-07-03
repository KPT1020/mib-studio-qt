# Hdf5Service

> Batched HDF5 read/write for experiment frames + metadata, plus
> frame-recording mode and review-time scalable (hyperslab) reads.
> HDF5 headers are hidden behind a PIMPL.

**Source:** `src/backend/recording/Hdf5Service.cpp`,
`include/backend/recording/Hdf5Service.h`
**Related:** [[ProcessingService]], [[../data-model/HDF5-Storage]],
[[../frontend/HdfReviewTab]], [[../architecture/AppBackend]]

## Async write decoupling — `HdfWriteQueue`

`include/backend/recording/HdfWriteQueue.h` is a header-only, bounded (3-slot)
single-writer queue used by **both** experiment flush ([[ProcessingService]]
`flushBufferedFrames`) and frame recording ([[../architecture/AppBackend]]
`startFrameRecording`) so slow HDF5 writes never stall the producer
(capture/collection). A dedicated writer thread drains the FIFO and calls an
injected `writeFn` (`appendFrames` / `appendRecordingFrames`). A failed write
**or** a `submit` when all 3 slots are in flight (disk too slow) is a fatal,
latched error: the writer stops, a one-shot `onError` fires, and further
`submit`s are rejected. The written/flushed counters advance only on a confirmed
successful write. `flushAndStop()` drains + joins for a clean stop. `Hdf5Service`
has no internal lock, so callers must ensure only one writer touches the open
file at a time — the queue's single writer thread provides that, and stop paths
drain the queue before any direct write.

`HdfWriteQueue`'s ctor parameter is `slotCount` (not `slots`, which Qt defines as
a macro). Unit-tested by `tests/backend/hdf_write_queue_test.cpp`.

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

- `openFile(path)` creates the destination's parent directory tree
  (`std::filesystem::create_directories`) before `H5Fcreate`. HDF5 cannot
  create intermediate directories, so without this a save to a folder that
  does not exist yet (e.g. a freshly chosen path on a second/external/network
  drive) fails — this was the root cause of "can save on one drive but not
  the other." Open failures now log an actionable message (drive available?
  writable? valid name? free space?) instead of a generic error. Regression
  guard: `tests/integration/e2e_storage_destinations_test.cpp`
  (`integration.e2e_storage_destinations`) saves to fresh/nested/long paths.
  Note: paths exceeding the Windows `MAX_PATH` (260) limit still fail unless
  long-path support is enabled at the OS level.
- `writeConfigJson` must follow `writeExperimentInfo`.
- **Append paths validate batch dimensions against the dataset extent**
  (`appendImageDataset`, `appendSeriesImageDataset`): the extent is fixed by
  the first-ever batch, so a mid-recording frame-size change fails loudly
  with a precise log message (surfaced via the fatal-save-error sink) instead
  of a cryptic `H5Dwrite` error. The series path also rejects a series image
  whose dims differ from the dataset's — its row-copy scratch buffer is sized
  from the dataset dims and a larger image would overflow the heap.
- `writeImageDataset` computes per-frame byte size in `size_t` (an `int`
  product would overflow for pathological frame sizes and undersize the
  staging buffer).
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
- **Rank guards before extent reads.** Every reader/appender validates
  `H5Sget_simple_extent_ndims` against the expected rank BEFORE calling
  `H5Sget_simple_extent_dims` into a fixed-size array — a foreign/corrupt
  file with a higher-rank dataset would otherwise smash the stack
  (regression test: `tests/recording/hdf5_foreign_rank_test.cpp`). The
  create-path writers (`writeImageDataset`, `writeSeriesImageDataset`)
  also reject mid-batch geometry changes loudly, mirroring the append
  path — skipping a mismatched frame silently desynced images[i] from
  metadata[i] and wrote uninitialized bytes.
