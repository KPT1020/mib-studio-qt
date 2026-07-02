# Realtime Performance: eliminate per-frame clones, locks, and O(n²) buffer churn

Status: active

## Goal

The frame hot paths (recording thread, realtime inline loop, experiment
accumulation, snapshot publication, display tick) do no avoidable full-frame
copies, no per-frame config/background deep copies, and no O(n) front-erases.
Improvements are quantified by extended `pipeline_timing_benchmark` parts that
gate on ratios (legacy-mirror vs shipped, per existing parts A/B style), and
every touched area carries its coverage-matrix tests
([`../../architecture/testing-strategy.md`](../../architecture/testing-strategy.md)).
The fully-implemented 2026-06-24 buffering plan is annotated completed.

## Background

Prior real-time work already shipped (do not re-plan): FrameStore two-tier
locking, `HdfWriteQueue` write decoupling + `FrameStore::reserveFrameBytes`,
trigger-callback hoisting/ordering, autofocus sort off the realtime thread,
`shared_ptr` contours + bbox brightness scan, HDF5 interval flush, bounded
experiment backlog + count-only status polling.

A 2026-07-02 source audit verified these remaining hot-path issues:

| ID | Issue | Where |
|----|-------|-------|
| P1 | Recording thread pays per frame: `ProcessingConfig` copy under `configMutex_`, ROI under `rtMutex_`, `getRealtimeBackgroundGray()` full-frame `clone()`, then `isFrameEmpty()` re-does a full-frame gray copy + 2× GaussianBlur | `src/backend/app/AppBackend.cpp:933-937`, `src/backend/processing/ProcessingService.cpp:358-363,518-560` |
| P2 | Monitoring accumulation clones `originalImage` + `processedImage` per detected object per frame into 1000-cap rings, even with no monitoring consumer active (N objects = 2N full-frame clones/frame) | `ProcessingService.cpp:1645-1667,1704-1705,2150-2152` |
| P3 | Experiment backlog trim is `vector::erase(begin())` under `framesMutex_` — O(n) shift per drop, O(n²) once the bounded backlog is full and every frame triggers a drop | `ProcessingService.cpp:996-1002,1052-1053` |
| P4 | Full-frame (experiment) path performs ~4-5 full-frame allocations/copies per saved frame (gray clone, full-size mask, redundant original/processed clones) | `ProcessingService.cpp` ~2064-2536 |
| P5 | Snapshot publication does full-frame `mask.clone()` (and at one site full-frame construction) **inside** `snapshotMutex_` every frame, serializing against display-FPS overlay reads | `ProcessingService.cpp:1680,2284-2314` |
| P6 | Display tick does two full-frame copies per 20 ms tick and `Qt::SmoothTransformation` rescale on every `paintEvent` on the UI thread | `src/frontend/tabs/OverviewTab.cpp:184-207`, `src/frontend/utils/SimpleImageCanvas.cpp:79` |
| P7 | Realtime loop copies the whole `ProcessingConfig` per frame and copies callback `std::function`s per validation object | `ProcessingService.cpp:1968-1976,1625-1641` |
| P8 | `FrameStore` frame-filter machinery is dead code (zero callers repo-wide); its `pushFrame` inspection block would cost a second full memcpy per frame if ever enabled; vault notes claim recording still uses it | `src/backend/playback/FrameStore.cpp:41-62,99-114` |
| P9 | No RT thread priority for trigger/realtime threads (documented limitation; e2e ≤50 ms gate currently met) | `TriggerService` vault note |

## Acceptance criteria

- [ ] `pipeline_timing_benchmark` gains parts (C) recording per-frame overhead,
      (D) experiment buffer trim under backpressure, (E) snapshot publish/read
      contention — each comparing a legacy mirror of the pre-fix code against
      the shipped path with loose ratio gates (no absolute ms).
- [ ] Recording thread does zero full-frame background clones and zero
      full-frame gray copies per frame during the empty-frame check (P1).
- [ ] Monitoring accumulation performs at most one buffer copy per frame
      regardless of object count, and none when no monitoring consumer is
      active (P2).
- [ ] Experiment backlog trim is O(1) per drop (`std::deque`), verified by
      benchmark part (D) at 10k-frame backlog (P3).
- [ ] Experiment full-frame path performs ≤2 full-frame allocations per saved
      frame (gray copy + full-size mask), zero redundant `.clone()` (P4).
- [ ] Snapshot publication holds no mutex across any full-frame copy; readers
      get an immutable `shared_ptr` snapshot (P5).
- [ ] Realtime loop copies `ProcessingConfig` only when it changed (generation
      counter), and callback `std::function` copies happen once per frame, not
      per object (P7).
- [ ] Display tick performs no per-tick heap allocation and
      `SimpleImageCanvas::paintEvent` rescales only when the frame or canvas
      size changed (P6).
- [ ] Dead `FrameStore` frame-filter machinery removed; vault notes corrected (P8).
- [ ] `e2e_trigger_timing_test` (≤50 ms gate), `e2e_realtime_throughput_test`,
      `e2e_live_view_latency_test`, `frame_store_concurrency_test`, and the
      TSan lane pass after every PR; frame-accounting conservation asserted
      where paths changed.
- [ ] `docs/superpowers/plans/2026-06-24-highspeed-capture-buffering.md`
      annotated completed; P9 recorded as a tech-debt entry with exit criterion.
- [ ] Vault updated per `knowledge_map/Vault-Maintenance.md` in each PR;
      `python3 scripts/check_docs.py` clean.

## Decision log

- 2026-07-02: PRs grouped by subsystem (recording / experiment / realtime-loop
  / display), not strictly by impact order, so each PR has one coherent
  TSan/stress surface and one benchmark part proving it. Recording first
  (highest per-frame avoidable cost, flagged since the 2026-04-16 audit),
  experiment path second (protects the trigger budget during experiments — the
  latency-critical scenario), always-on realtime loop third, display last
  (frontend, needs manual Windows build).
- 2026-07-02: Clone elimination relies on cv::Mat refcount sharing. Verified
  safe today: every hot-path Mat (`gray`, `mask`, `fullMask`, `grayROI`) is
  freshly allocated per loop iteration and never written after publication,
  and all consumers (`Hdf5Service::appendFrames`, monitoring/HDF readers,
  overlay) are read-only. This becomes an explicit invariant — "Mats published
  from the realtime loop are frozen" — enforced by comment + invariant test,
  since future in-place buffer reuse would silently break it.
- 2026-07-02: `validFrames_`/`invalidFrames_` become `std::deque`;
  `ExperimentBatch` keeps `std::vector` (`Hdf5Service::appendFrames` signature
  unchanged) — flush converts via `std::make_move_iterator` (moves are
  shallow, cv::Mat is refcounted). `getValidFrames()` keeps returning
  `std::vector` (it already copies).
- 2026-07-02: P9 (RT thread priority) deferred to tech-debt: platform-specific,
  priority-inversion risk, and the ≤50 ms e2e gate is currently met. Exit
  criterion: opt-in priority raise for trigger + realtime threads validated by
  `e2e_trigger_timing_test` on hardware.

## PR breakdown

### PR1 — Measurement first + housekeeping (no behavior change)

- `tests/performance/pipeline_timing_benchmark.cpp`: add parts **(C)**
  recording per-frame overhead (per-frame `getProcessingConfig` + background
  clone + full-frame `isFrameEmpty` vs hoisted/ROI path), **(D)** buffer trim
  at 10k backlog (vector front-erase vs deque `pop_front`), **(E)** snapshot
  publish/read contention (mutex-held `mask.clone()` publish vs pointer swap,
  under a hammering reader). Legacy mirrors are in-test copies of the current
  code, exactly how parts (A)/(B) lock in the FrameStore and brightness-scan
  wins. Register under the existing `performance.pipeline_timing` CTest.
- This plan file committed; 2026-06-24 buffering plan annotated
  `Status: completed`; tech-debt row for P9.
- Risk: none (test-only + docs). Tests: the new benchmark parts.

### PR2 — Recording hot path (P1) + dead filter removal (P8)

- `ProcessingService`: add
  `std::shared_ptr<const cv::Mat> getRealtimeBackgroundGrayShared() const`
  (`rtBgGray_` is already a `shared_ptr`, `include/backend/processing/ProcessingService.h:470`;
  `setRealtimeBackgroundGray` replaces the pointer wholesale, so shared reads
  are safe). Keep the cloning getter for cold callers (`MainWindow`,
  `ExperimentController`, `BufferSaveDialog`).
- Add `std::atomic<uint64_t> configVersion_` bumped in `setProcessingConfig` /
  `setRealtimeRoi`; expose `getConfigVersion()`.
- `AppBackend.cpp` recording lambda (~905-975): hoist config/ROI/background out
  of the per-frame loop; refresh once per poll iteration or on version change
  (precedent: line ~870 already hoists a config copy for multi-image metadata).
  Config staleness window grows from per-frame to per-poll-batch (~ms) —
  acceptable, documented.
- `isFrameEmpty`: ROI overload reusing `makeGrayROI` (promote from file-static
  to shared helper) — full-frame blur becomes ROI-sized; background input is
  `(*bg)(cvRoi)` from the shared ptr, no clone. Old signature stays as a thin
  wrapper.
- **P8**: delete `FrameStore::setFrameFilter/clearFrameFilter/hasFrameFilter`,
  `frameFilter_`, and the `pushFrame` inspection block; fix stale vault claims
  (`knowledge_map/architecture/AppBackend.md`, `knowledge_map/data-model/FrameStore.md`).
- Tests (real-time + FrameStore ⇒ latency budget + invariant + TSan + stress):
  benchmark part (C) flips to legacy-vs-shipped with ratio gate; recording
  frame-accounting invariant (scanned == written + filtered + skipped);
  ROI-`isFrameEmpty` classifies identically to the full-frame version on
  synthetic frames; `frame_store_concurrency_test` / `frame_store_bounds_test`
  / `frame_store_reserve_test`; TSan lane; `e2e_realtime_throughput_test`.
- Vault: `services/ProcessingService.md`, `architecture/AppBackend.md`,
  `data-model/FrameStore.md`, mark the background-clone item resolved in
  `task/2026-04-16-thread-performance-audit.md` "Not fixed".

### PR3 — Experiment path (P3 deque + P4 clone elimination)

- `validFrames_`/`invalidFrames_` → `std::deque<ProcessedFrame>`;
  `trimExperimentBuffersLocked` and the inline drop in `appendExperimentFrame`
  use `pop_front()`; `flushBufferedFrames` fills `ExperimentBatch` vectors via
  `assign(make_move_iterator(...))`; `getValidFrames/getInvalidFrames` copy
  deque→vector.
- Replace redundant `.clone()`s with shallow refcounted assigns: full-frame
  loop (~2535-2536), multi-image start (~2908-2910), ROI path (`makeFullGray`
  extra clone; second clones at ~2242-2244 / ~2268-2269 on freshly-built Mats).
  Snapshot-dims fix: use `f.height`/`f.width` instead of building a gray copy
  just to read rows/cols (~2295-2307).
- Add the frozen-Mats invariant comment at the top of `realtimeInlineLoop`.
- Expected impact: ≤2 full-frame allocations per saved frame; O(n²)→O(n) trim;
  smaller `framesMutex_` hold times (less contention with 500 ms status polls).
- Tests: benchmark part (D); `e2e_realtime_throughput_test` extended with
  experiment active + saturated `maxBufferedFrames_`, asserting steady-state
  throughput + accounting conservation (appended == flushed + droppedValid +
  droppedInvalid); pixel-equality invariant test for shallow shares (flushed
  frame's pixels equal the processed frame's pixels after the source loop
  iteration completed); `e2e_pipeline_stress_test` + TSan;
  `e2e_trigger_timing_test` gate unchanged.
- Vault: `services/ProcessingService.md` (deque, sharing invariant),
  `architecture/Data-Flow.md` if flush description mentions vectors.

### PR4 — Always-on realtime loop (P2 monitoring + P5 snapshot + P7 hoists)

- **P2**: clone ROI Mats **once per frame** and share the refcounted Mats
  across all N object entries (ring entries are read-only; `toVector()`
  shallow-copies). Add `std::atomic<bool> monitoringActive_` +
  `setMonitoringActive(bool)`; `ExperimentMonitoringTab` enables on
  show/poll-start, disables on hide. When inactive,
  `appendRealtimeMonitoringFrame` / `publishRealtimeBatchFrame` return before
  any clone.
- **P5**: `latestSnapshot_` → `std::shared_ptr<const RealtimeSnapshot>`;
  producer builds the snapshot outside any lock (moving the already-final
  mask — no clone) and pointer-swaps under `snapshotMutex_`.
  `getLatestSnapshot(RealtimeSnapshot&)` keeps its signature and
  shallow-copies from the immutable snapshot (read-only use in `PlaybackPanel`
  verified).
- **P7**: realtime loop caches `ProcessingConfig` + last-seen `configVersion_`
  (re-copy only on change); hoist the callback `std::function` copies out of
  the per-object loop (one copy per frame).
- Behavior note: monitoring rings fill only while the tab is active —
  documented in the tab's vault note (the tab already clears explicitly).
- Tests: benchmark part (E) with ratio gate; `e2e_live_view_latency_test`
  extended with a display-rate snapshot reader; no-tearing invariant (snapshot
  index/mask/contours always from the same frame); monitoring-gating invariant
  (buffers stay empty when inactive, fill when active);
  `e2e_trigger_timing_test` (callback code touched — latency-budget category);
  TSan + stress.
- Vault: `services/ProcessingService.md` (snapshot model, monitoring gating),
  `frontend/ExperimentMonitoringTab.md`, `architecture/Threading-Model.md`,
  mark the snapshot item resolved in the 2026-04-16 audit note.

### PR5 — Display tick (P6; frontend, manual Windows build)

- `OverviewTab::onTick`: member scratch `Frame` (vector capacity reused) +
  persistent `QImage` of matching size/format with row-copy into `bits()` —
  removes per-tick allocations, keeps two copies total.
- `SimpleImageCanvas::paintEvent`: cache the scaled `QImage`; recompute only
  when the image cacheKey or draw size changed (paintEvents also fire for
  overlay drags/resizes — each currently pays a full smooth rescale).
- Optional, benchmark-gated: `FrameStore::copyLatestInto(...)` writing from
  the ring slot directly into caller memory under the slot lock, collapsing to
  one copy — only if measurement justifies the backend API; if added, extend
  `frame_store_concurrency_test` (no-torn-frame invariant).
- Verification: backend CI for any FrameStore addition; manual Windows build +
  50 fps tick / ROI-drag smoothness check noted in the PR.
- Vault: create `knowledge_map/frontend/OverviewTab.md` (+ `_MOC` link per the
  new-note rule) or fold into the existing display note;
  `data-model/FrameStore.md` if the API is added.

### PR6 — P9 disposition (tech debt row, done in PR1's commit)

Tech-debt-tracker row: opt-in RT priority for the trigger + realtime inline
threads (Windows `SetThreadPriority`, best-effort `pthread_setschedparam`),
default off, exit criterion = hardware-validated `e2e_trigger_timing_test` +
documented rollback. The EGrabber `stop()` ~360 ms sleep under `stateMutex_`
is shutdown-only latency — noted in the row, not fixed here.

## Progress

- [ ] PR1: benchmark parts C/D/E + 2026-06-24 plan annotated + this plan committed
- [ ] PR2: recording hot path (P1) + FrameStore filter removal (P8)
- [ ] PR3: experiment deque (P3) + clone elimination (P4)
- [ ] PR4: monitoring gating (P2) + snapshot shared_ptr (P5) + config/callback hoists (P7)
- [ ] PR5: display tick + canvas scale cache (P6)
- [ ] PR6: tech-debt entry for RT thread priority (P9)
