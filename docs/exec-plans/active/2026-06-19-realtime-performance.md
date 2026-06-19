# Real-time Performance Remediation

Status: active

## Goal

The live pipeline (camera grab → `FrameStore` → `ProcessingService` /
recording → frontend render) sustains high frame rates without per-frame heap
churn, without synchronous disk I/O on hot threads, and without the Qt GUI
thread doing full-frame OpenCV work every tick. When done, recording and
capture threads do at most one full-frame copy per frame, HDF5 flushes are
decoupled from the write batch, and monitoring/preview widgets update
incrementally at a bounded rate.

This plan is the output of the 2026-06-19 real-time performance scan. Findings
are referenced below as F1–F10.

## Findings recap (severity)

| ID | Severity | Location | Problem |
|----|----------|----------|---------|
| F1 | 🔴 | `FrameStore.cpp:32-77` | Filter path copies whole frame to a temp, then copies again into the ring — 2 full copies/frame on capture thread when filtering is on |
| F2 | 🔴 | `EGrabberCamera.cpp:282-283` | `resize`+`copy_n` heap-allocates a fresh full-frame buffer every grab |
| F3 | 🔴 | `AppBackend.cpp:883-908`, `ProcessingService.cpp:358-363` | Recording loop does ~4 full-frame copies/frame: `getByWriteIndex` copy + `getRealtimeBackgroundGray()` clone + `makeGrayCopy` + `view.clone()`, plus 2 GaussianBlurs, with per-frame config/roi/bg lookups |
| F4 | 🔴 | `MindVisionCamera.cpp:407` | `out.data.assign(...)` full-frame copy/alloc per grab |
| F5 | 🟠 | `Hdf5Service.cpp:1099-1102,1375,1434` | `H5Fflush(H5F_SCOPE_GLOBAL)` after every batch; `FLUSH_BATCH=50` has no time cap (`AppBackend.cpp:858,920`) |
| F6 | 🟠 | `Hdf5Service.cpp:869-902` | Multi-image series writes are N×series single `H5Dwrite` calls (known stop-lag suspect) |
| F7 | 🟠 | `ProcessingService.cpp:1985,2964-2965` | Per-frame full `ProcessingConfig` copy under lock; redundant `mask` assign-then-clone for snapshot |
| F8 | 🟠 | `ExperimentMonitoringTab.cpp:603-924` | Charts + thumbnail grid fully cleared and rebuilt every tick (new axes/`QBarSet`, `findContours`, `SmoothTransformation` ×24) |
| F9 | 🟠 | `PlaybackPanel.cpp:979-1099` | Full CV pipeline (blur/threshold/morph/contours) per frame on the GUI thread |
| F10 | 🟡 | `OverviewTab.cpp:184-208`, `SimpleImageCanvas.cpp:78-80` | Polls/scales every timer tick with `SmoothTransformation` and `img.copy()` regardless of new-frame arrival |

## Acceptance criteria

- [ ] Capture thread performs ≤1 full-frame copy per frame whether or not a
      frame filter is set (F1).
- [ ] Recording thread performs ≤1 full-frame copy per frame; config/ROI/
      background are not re-fetched or cloned per frame (F3).
- [ ] Camera grab paths reuse buffers across frames instead of allocating a new
      one each grab (F2, F4).
- [ ] HDF5 global flush frequency is decoupled from the write batch and bounded
      by time; recording batch growth has a time cap (F5).
- [ ] Multi-image series write issues O(batch) writes, not O(batch×series) (F6).
- [ ] Monitoring tab and preview/overview canvases update incrementally and at a
      bounded rate; no full-frame OpenCV pipeline runs on the GUI thread (F8,
      F9, F10).
- [ ] `python3 scripts/check_docs.py` passes; `ctest --preset
      linux-backend-only-test` passes; vault notes updated per
      `knowledge_map/Vault-Maintenance.md` in each PR.

## Approach by PR

### PR 1 — Backend hot-path copies (F1, F3, F7) — low risk, high leverage

Backend-only, covered by existing unit tests, no API changes visible to the
frontend.

1. **F1 `FrameStore::pushFrame`**: when `frameFilter_` is set, stage the frame
   data once into a local `Frame`, run the filter on it, and on accept *move*
   its `data` into the ring slot (swap the slot's vector) instead of copying a
   second time. Keep the existing `resize`+`copy_n` fast path (which reuses the
   slot's capacity) when no filter is set. Net: 1 copy with or without a filter.
2. **F3 recording loop** (`AppBackend.cpp`): add a `configGeneration_`
   `std::atomic<uint64_t>` to `ProcessingService`, bumped in
   `setProcessingConfig` / `setRealtimeRoi` / `setRealtimeBackgroundGray`. The
   recording loop caches `config`/`roi`/`bg` (the latter as a `shared_ptr`
   without cloning) and refreshes only when the generation changes. Add a
   `getRealtimeBackgroundShared()` returning the existing `shared_ptr<cv::Mat>`
   so the per-frame `clone()` in `getRealtimeBackgroundGray()` is avoided on the
   hot path. Reuse one `cv::Mat` scratch buffer for the empty-frame check.
3. **F7**: hoist the redundant `latestSnapshot_.mask = mask; ... .clone();` to a
   single `.clone()`. Leave the per-frame `ProcessingConfig` copy unless
   profiling shows it matters (POD copy ≪ OpenCV cost); note it in the decision
   log.

Verify: `ctest --preset linux-backend-only-test`; add/extend a FrameStore test
asserting filtered + unfiltered paths both store correct data and that an
accepted filtered frame matches the source bytes. Vault: update
`knowledge_map/data-model/FrameStore.md`,
`knowledge_map/services/ProcessingService.md`,
`knowledge_map/architecture/Overview.md` (threading/data-flow note).

### PR 2 — Camera buffer reuse (F2, F4) — medium risk

Introduce a small reusable-buffer pool inside each camera so the per-grab
`resize`/`assign` reuses prior capacity instead of allocating.

1. **F2 `EGrabberCamera`**: maintain a free-list (`std::vector<std::vector<
   uint8_t>>`) of reclaimed `Frame` buffers. `replenishPendingFrames` pops a
   buffer (retaining capacity), resizes, copies the GenTL part, and pushes the
   `Frame`. When `grabFrame` hands a frame to the caller, the consumed buffer's
   capacity is recycled back to the free-list on the next cycle (caller still
   owns its copy in `FrameStore`). The GenTL→host copy itself is unavoidable;
   the goal is to remove the allocation, not the memcpy.
2. **F4 `MindVisionCamera`**: same recycling approach for `out.data`.

Verify: backend tests + a mock-camera soak run (`MIB_CAMERA_MODE=mock`)
confirming steady-state allocation counts via existing memory logging. Vault:
update `knowledge_map/services/CaptureService.md` and the camera notes.

### PR 3 — HDF5 I/O decoupling (F5) — medium risk

1. Throttle `H5Fflush(H5F_SCOPE_GLOBAL)`: flush at most once per N ms (config,
   default ~200 ms) and always on `stop()`/`close()`. Dataset writes still
   happen per batch; only the explicit global flush is rate-limited.
2. Add a time cap to the recording batch in `AppBackend.cpp`: flush when
   `batchImages.size() >= FLUSH_BATCH` **or** `>= maxBatchDelayMs` elapsed, so a
   slow flush cannot let the batch grow unbounded.

Verify: HDF5 round-trip tests (write then read back) for integrity; measure
stop-latency before/after with multi-image disabled. Vault: update
`knowledge_map/services/Hdf5Service.md`, `knowledge_map/services/RecorderService.md`.

### PR 4 — Multi-image HDF5 write batching (F6) — higher risk, isolated

Replace the N×series single-`H5Dwrite` nested loop in
`appendSeriesImageDataset` with a contiguous staging buffer per batch and a
single (or per-frame) hyperslab write. Gate behind the existing multi-image
path so non-multi-image recording is unaffected.

Verify: dedicated HDF5 test writing a known multi-image series and reading it
back byte-for-byte; stop-latency measurement with multi-image enabled. Update
the diagnostic comment that currently flags this as the stop-lag suspect.

### PR 5 — Frontend GUI-thread work (F8, F9, F10) — medium risk, frontend-only

1. **F10**: in `OverviewTab`/`PreviewPage` canvases, skip work when the frame
   index is unchanged; cache the scaled `QImage`/`QPixmap` and invalidate only on
   resize or new frame; drop the per-tick `img.copy()`.
2. **F8 `ExperimentMonitoringTab`**: incremental chart append (track last index,
   append only new points; reuse axes and `QBarSet` instead of recreating);
   widget-pool the thumbnail grid (reuse `QLabel`s, only swap pixmaps); split
   timers so scatter/histogram run ~10 Hz and the grid ~5 Hz; use
   `Qt::FastTransformation` for thumbnails.
3. **F9 `PlaybackPanel`**: move `computeProcessedOverlay` off the GUI thread (a
   worker that posts results via queued connection) and cache the overlay when
   the source frame is unchanged; preallocate scratch `cv::Mat`s.

Verify: manual run via the `run` skill / mock camera, confirm UI stays
responsive and frame display keeps up; check no overlay regressions. Vault:
update `knowledge_map/frontend/` notes for the touched widgets.

### PR 6 — Cleanup / follow-ups (optional)

- `FrameStore::getByWriteIndex` zero-copy view/handle variant for readers that
  do not need ownership.
- Async load of isoelastic CSV at tab construction.
- Replace polling timers (`PreviewPage` 300 ms, stats 500 ms) with
  signal-driven updates where practical.
- De-duplicate the three near-identical ~250-line blocks in
  `realtimeInlineLoop` into one helper to prevent divergent optimizations.

Anything not landed here moves to `docs/exec-plans/tech-debt-tracker.md`.

## Test plan

Conventions to follow (from `tests/`): tests are plain executables linked to
`mib_backend`, registered in `tests/CMakeLists.txt` via `add_test` with `LABELS`,
run by `ctest --preset linux-backend-only-test`; they return non-zero on
failure and print human diagnostics. **Every perf fix ships a benchmark that
keeps an inline _legacy_ baseline mirroring the pre-fix design, asserts the new
path is byte-identical, and adds a loose (non-flaky) throughput bound** — the
pattern already used by `tests/performance/pipeline_timing_benchmark.cpp` and
`tests/backend/frame_store_concurrency_test.cpp`. Frontend logic is made
testable by extracting pure helpers (no event loop) into free functions.

### PR 1 — backend hot-path copies (F1, F3, F7)

- **`tests/backend/frame_store_filter_copy_test.cpp`** (new, label `backend`):
  install a `frameFilter_` that rejects frames by a predicate (e.g. timestamp
  parity). Push a mixed stream and assert: (a) accepted frames returned by
  `getByWriteIndex` are byte-identical to the source and carry correct
  width/height/linePitch/pixelFormat/timestamp — proves the move-on-accept path
  preserves data; (b) `totalWritten()` counts only accepted frames and
  `totalFiltered` counts rejects; (c) the no-filter fast path is unchanged.
- **Extend `frame_store_concurrency_test.cpp`**: run a second pass with an
  accept-all filter installed so the move-into-slot path is exercised under the
  1-producer/N-consumer torn-read invariant.
- **`tests/backend/processing_config_generation_test.cpp`** (new, label
  `processing;backend`): assert `configGeneration_` increments on
  `setProcessingConfig` / `setRealtimeRoi` / `setRealtimeBackgroundGray`, that
  `getRealtimeBackgroundShared()` returns the same `shared_ptr` (pointer
  identity, no clone), and that a background swap is observed by a cache that
  refreshes on generation change.
- **Extend `recording_lifecycle_test.cpp`**: change ROI/background mid-recording
  and assert the recorder's empty-frame decision reflects the new config within
  the refresh window — guards against the cache going stale.
- **Snapshot ownership** (extend `processing_pipeline_smoke_test.cpp`): after a
  realtime tick, mutate the internal mask and assert the published snapshot mask
  is unaffected — proves the single-clone still yields an independent buffer.

### PR 2 — camera buffer reuse (F2, F4)

- Extract the recycling pool as a standalone, SDK-free class (e.g.
  `FrameBufferPool`) so it is testable without hardware.
- **`tests/backend/frame_buffer_pool_test.cpp`** (new, label `camera;backend`):
  assert correctness (a checked-out buffer holds exactly the bytes written) and
  reuse (after warm-up, steady-state acquisitions cause no capacity growth /
  reallocation — track `capacity()` and an allocation counter).
- **Extend `mock_camera_smoke_test.cpp`**: soak N frames and assert frame data
  integrity across the recycled path; EGrabber/MindVision stay hardware-gated
  and rely on the pool unit test plus a manual `MIB_CAMERA_MODE=mock` soak that
  watches the existing memory log for flat steady-state RSS.

### PR 3 — HDF5 flush decoupling (F5)

- **Extend `recording_lifecycle_test.cpp`**: (a) write frames and read them back
  byte-identical to prove throttled flush keeps integrity; (b) stop after fewer
  frames than the flush interval and assert the file is complete/readable —
  proves flush-on-stop; (c) simulate slow arrival and assert the time-cap forces
  a flush before the batch fills (observe batch counter / file growth).

### PR 4 — multi-image write batching (F6)

- **`tests/recording/recording_multi_image_test.cpp`** (new, label
  `recording;backend`): write a known multi-image series and read each series
  image back byte-for-byte. Keep an inline legacy nested-write reference and
  assert the batched output is identical (golden). Add a loose stop-latency
  bound (batched ≤ legacy within margin) to lock in the stop-lag fix.

### PR 5 — frontend GUI-thread work (F8, F9, F10)

- Extract pure decision helpers and unit-test them in
  **`tests/frontend/realtime_render_helpers_test.cpp`** (new): frame-skip
  decision (skip when index unchanged), scaled-image cache key/invalidation
  (recompute only on size or source change), and incremental chart-append index
  tracking (only new points appended, monotonic, bounded).
- Manual verification via the `run` skill / mock camera: UI stays responsive at
  target frame rate, overlay output unchanged vs `main` (spot-check), thumbnail
  grid updates without flicker.

### Cross-cutting regression guard

- **Extend `tests/performance/pipeline_timing_benchmark.cpp`** with two new
  new-vs-legacy comparisons following the existing harness: F1 (filtered
  `pushFrame`: one copy vs two) and F3 (recording per-frame cost: cached
  config/shared bg vs per-frame fetch+clone). Each asserts bit-identical results
  and a loose speedup floor so a regression that reintroduces the extra copies
  fails CI.
- Register every new test in `tests/CMakeLists.txt` with labels; the full set
  must pass under `ctest --preset linux-backend-only-test`. Performance tests
  get a `TIMEOUT` like the existing benchmark (180s).

## Decision log

- 2026-06-19: Sequenced backend copy fixes (PR 1) first — highest leverage,
  lowest risk, and exercised by existing backend CI. Camera buffer pooling and
  HDF5 changes follow because they carry hardware/I/O risk. Frontend is
  independent and can proceed in parallel with PR 2–4.
- 2026-06-19: Chose a `configGeneration_` counter over caching-by-value-each-
  frame for the recording loop so the loop reflects live config changes without
  paying clone/lock cost on every frame.

## Progress

- [ ] PR 1 — backend hot-path copies (F1, F3, F7)
- [ ] PR 2 — camera buffer reuse (F2, F4)
- [ ] PR 3 — HDF5 flush decoupling (F5)
- [ ] PR 4 — multi-image write batching (F6)
- [ ] PR 5 — frontend GUI-thread work (F8, F9, F10)
- [ ] PR 6 — cleanup / follow-ups
