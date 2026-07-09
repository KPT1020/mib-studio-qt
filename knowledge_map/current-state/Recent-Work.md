# Recent Work

> Snapshot of recently merged features and fixes, as of 2025-11 / 2025-12.
> Refresh from `git log --oneline -20` when outdated.

## Features shipped

- **HDF review export naming and batch export** (2026-07-09) -
  `HdfReviewTab` now suggests source-derived metrics filenames
  (`<h5-basename>_metrics.csv`) with collision suffixes, writes Export All
  output into source-specific folders, adds batch Metrics and batch Export All
  actions for multiple `.h5` / `.hdf5` files, remembers one shared successful
  output directory with `QSettings`, and reports per-file batch failures in a final
  summary. The standalone Python exporter and PySide wrapper now share the
  source-derived output policy: CSV-only writes `<h5-basename>_metrics.csv`,
  image/all exports write under a collision-safe `<h5-basename>/` folder, and
  `--output` remains directory-only. Added `frontend.hdf_review_export_paths`
  and `scripts.export_hdf5_paths` coverage for basename, suffix, folder, and
  output-root validation policy.

- **Realtime-performance benchmark parts C/D/E** (2026-07-02, PR1 of
  `docs/exec-plans/active/2026-07-02-realtime-performance.md`) —
  Extended `tests/performance/pipeline_timing_benchmark.cpp` (CTest
  `performance.pipeline_timing`) with three new parts proving the planned
  optimizations are real before any behavior is changed. **(C)** recording
  per-frame overhead: per-frame config lock + full-frame background clone +
  full-frame `isFrameEmpty` vs hoisted config/shared-ptr + ROI-only
  `isFrameEmpty` (1280×1024 frame, 128×128 cell ROI; measured ~4.6× speedup
  on a 24-core box). **(D)** experiment buffer trim: `vector::erase(begin())`
  at 10k-frame backlog vs `deque::pop_front()` (measured ~1500× speedup,
  proving the O(n²) steady-state cost). **(E)** snapshot publish/read
  contention: mutex-held `mask.clone()` publish vs pointer-swap with clone
  outside the mutex, under a hammering reader pool (measured ~64× reader
  throughput increase). Gates are loose and non-flaky (C: speedup ≥1.3×; D:
  ≥10×; E: reader throughput ≥0.5× legacy). No behavior change; test-only.

- **Crash-hardening: input/IO batch** (2026-07-02) — `EModulusLut` rejects
  degenerate LUT files (constant area/deform column → zero grid step →
  `size_t(floor(NaN))` UB indexing `grid_` out of bounds) and clamps lookup
  indices (new `backend.emodulus_lut_degenerate` test); `Hdf5Service` append
  paths validate batch dims against the dataset extent and series-image dims
  against the scratch buffer (heap overflow otherwise); `MainWindow`'s async
  flush captures the backend pointer instead of `this` and the destructor
  waits for an in-flight flush; `MockCamera::refreshFileList` uses the
  `error_code` `directory_iterator`; realtime loops resync a cached
  `rtLastProcessed_` that lands beyond `latest` after a `FrameStore::resize`;
  `HdfReviewTab` nav state is a `shared_ptr` instead of
  `new`/`delete`-in-connect.

- **Crash-hardening: frame buffer geometry + FrameStore identity** (2026-07-02) —
  camera buffers are validated where produced (`replenishPendingFrames`
  rejects null-base/short/garbage-size SDK buffers) and where consumed
  (`makeGrayCopy`/`makeGrayROI` in ProcessingService, the recording thread's
  strided view, and `FrameStore::getByWriteIndexROI` + AVI/TIFF exports all
  check `data.size() >= (h-1)*pitch + w` before building strided views —
  previously a pitch/size mismatch read out of bounds on the hot path).
  `FrameStore` reads also re-verify frame identity under the slot lock via a
  new `slotWriteIndices_` array, closing a TOCTOU where a wrapping producer
  (or a reader arriving before the producer's copy) returned a
  self-consistent but *wrong* frame — possibly with different geometry —
  under the requested index. Extended `frame_store_bounds_test` (pitch
  mismatch) and `frame_store_concurrency_test` (identity assertions).

- **Crash-hardening: trigger/camera stop race** (2026-07-02) —
  `CaptureService::stop()` (GUI thread) now stops the trigger thread via
  `cameraReadyCallback_(nullptr)` before `activeCamera_->stop()`, and
  `EGrabberCamera` guards every `grabber_` assignment/reset plus the
  trigger-thread `setTriggerOutput` read with a dedicated `triggerMutex_`
  (`running_` is now `std::atomic<bool>`). Previously a pending trigger pulse
  during a GUI-initiated camera stop could call into a half-destroyed
  grabber (use-after-free inside the Euresys SDK). Corrects the 2026-04-16
  thread-audit F4 assumption that camera lifecycle only runs on the capture
  thread.

- **Crash-hardening: ProcessingService exception containment** (2026-07-02) —
  worker jobs, batch workers, and the realtime loop now catch and log
  exceptions (dropping the failing job/batch or restarting the loop) instead
  of letting them escape the thread entry function and `std::terminate` the
  process on one bad frame or a throwing `cv::` call. New
  `tests/processing/processing_fault_injection_test.cpp` injects throwing
  jobs and callbacks and asserts the service keeps processing.

- **Crash-hardening: self-sufficient backend shutdown** (2026-07-02) —
  `~ProcessingService` now calls `stopRealtime()` (a joinable
  `realtimeThread_` at destruction previously hit `std::terminate`), and
  `AppBackend::shutdown()` (called from `~AppBackend`) stops capture →
  trigger → recording → processing before member destruction, closing a
  use-after-free where the realtime loop's callbacks fired into
  already-destroyed `triggerService_`/`autofocusService_` on any exit path
  that bypassed `MainWindow::closeEvent`. Regression coverage in
  `tests/backend/backend_lifecycle_smoke_test.cpp`.

- **Real-time performance examination + remediation plan** (2026-07-02) —
  Audited every component on the real-time hot path and committed
  `docs/exec-plans/active/2026-07-02-realtime-performance.md`, a 6-PR plan
  covering the remaining per-frame costs: recording-thread background
  clone/config copy + full-frame `isFrameEmpty` (P1), per-object monitoring
  clones (P2), O(n²) experiment-buffer trim (P3), redundant experiment-path
  clones (P4), mutex-held snapshot clone (P5), display-tick copies/rescale
  (P6), per-frame config/callback copies (P7), dead `FrameStore` frame-filter
  (P8), and RT thread priority (P9 → new tech-debt row TD-7). Also annotated
  the fully-implemented 2026-06-24 high-speed-capture-buffering plan
  `Status: completed`. Details in
  `knowledge_map/task/2026-07-02-realtime-performance-plan.md`. Docs-only
  change; code lands via the plan's PRs.

- **OverviewTab ROI propagation to recording** (2026-06-29) — `MainWindow`
  now connects `OverviewTab::roiChanged` to
  `ProcessingService::setRealtimeRoi()`, so the recording thread crops
  frames to the OverviewTab ROI instead of saving the full camera frame.
  The startup initialization block also seeds the processing ROI from the
  current OverviewTab values. Files: `src/frontend/core/MainWindow.cpp`.

- **HDF5 recording finalization hardening** (2026-06-26) - `Hdf5Service`
  now creates writable HDF5 files with strong-close semantics and performs an
  explicit final global flush before `H5Fclose`, logging final flush status,
  close timing, and open-object count. `AppBackend::startFrameRecording`
  only increments the recorded-frame counter after successful HDF5 appends,
  so `/recording_info` matches persisted batches. This targets stale
  superblock/EOA failures observed in recording-mode `.h5` files that required
  `h5clear --increment` repair. The per-append `.recovery.h5` full-file copy
  was **removed** (it copied the whole growing file every batch and made the
  recorder thread fall behind on NAS, dropping frames); append paths now flush
  on a time interval via `maybeIntervalFlush()` (`MIB_HDF5_FLUSH_INTERVAL_MS`,
  default 5000 ms) and there is no recovery sidecar. A mid-recording crash can
  lose up to one flush interval — the accepted tradeoff for real-time
  throughput. `recording.hdf5_resilience` covers destructor-driven
  finalization, clean-fail on a corrupted primary, and data preservation for
  recording-mode and experiment files; `recording.hdf5_save_performance` guards
  repeated-append save time.

- **Cloudflare R2 profile catalog publishing setup** (2026-06-11) — Added
  `publish-profiles.py` for KIN-47 profile catalog hosting under
  `https://updates.yofo.bio/profiles/<channel>/catalog.json`. The publisher
  validates `config_schema_version`, computes config/script SHA-256 values,
  generates upload-time `profile.meta.json`, writes `catalog.json`, and reuses
  the existing Wrangler/S3-compatible R2 credential flow. `docs/howto/auto-update-r2.md`
  now documents the required Cloudflare public bucket, cache, verification,
  and no-credential-in-repo setup for remote profile catalogs.

- **Pipeline timing benchmark** (2026-06-17) — `tests/performance/pipeline_timing_benchmark.cpp`
  (ctest `performance.pipeline_timing`) quantifies the two throttling fixes
  below. (A) Runs the shipped per-slot `FrameStore` against an in-test
  `LegacyRing` (single global mutex held across the full-frame copy) under
  1 producer + N consumers — measured ~1.6–1.8× higher full-frame-copy
  throughput on a 4-core box, and ~1.8× faster producer pushes (capture no
  longer blocked behind consumers). (B) A/Bs the new bbox/row-pointer
  brightness scan vs the old full-ROI `cv::Mat::at<>` scan, asserting the
  quantiles are **identical** across 4000 cases (~1.6× faster). Gates are
  loose (throughput ≥ 0.5× legacy; brightness identical) so CI does not flake.

- **Per-frame detection allocator/CPU cost reduction** (2026-06-17) —
  follow-up to the FrameStore fix targeting single-thread algo time in
  [[../services/ProcessingService]]. (1) `FilterResult::allContours` is now a
  `shared_ptr<const ...>` assigned once per frame instead of deep-copying the
  whole contour set into every object's result (and again into each monitoring
  / experiment copy); the write-only `hierarchy` field was deleted. (2)
  `calculateBrightnessQuantiles` now scans only the object's bounding box via
  row pointers and drops the needless `clone()` for gray input. Both costs
  previously scaled linearly with objects-per-frame — the busy/triggering case.
  Behaviour is bit-identical; covered by the existing multi-object / tracking /
  integration tests.

- **FrameStore lock-contention throttling fix** (2026-06-17) — the realtime
  image-processing and triggering pipeline was throttling because
  [[../data-model/FrameStore]] used a single `std::mutex` held *across the
  full-frame `memcpy`* on both `pushFrame` (capture) and every `get*`
  consumer (realtime loop, UI preview, raw-frame recorder). Producer and
  consumers serialised on that one lock, defeating the ring buffer's
  decoupling and stalling capture/processing/triggering. Replaced with a
  two-tier scheme: a `std::shared_mutex structureMutex_` (shared on the hot
  path, exclusive for `resize` / save / estimate) plus a per-slot
  `std::mutex` array so the copy in/out holds only that slot's lock. Also
  removed a redundant second `getByWriteIndex` of the same index in the
  realtime snapshot path (`ProcessingService::realtimeInlineLoop`) — it now
  reuses the already-fetched frame. See [[../data-model/FrameStore]]
  "Threading".

- **Experiment multi-image capture mode guard** (2026-06-16) —
  `MainWindow::onStartExperiment` now auto-switches realtime processing from
  `async_batch` to `inline` when multi-image capture is enabled, so experiment
  runs actually collect series frames that can be viewed in Review. The
  previous realtime mode is restored in `onStopExperiment`.

- **Recording review multi-image window support** (2026-06-16) —
  `writeRecordingInfo` now persists `multi_image_enabled` and
  `multi_image_count` in `/recording_info`; `HdfReviewTab` reads those
  attributes and, for recording files, loads a bounded series window from
  `/recorded_frames/images` into `FrameViewerDialog` so series navigation is
  available during review.

- **Multi-image export range selector in Review tab** (2026-06-16) —
  `HdfReviewTab` `Export All` now detects `series_images` datasets and prompts
  users to export all series frames, a custom 1-based range (for example
  `9-15`), or skip series images entirely. The export summary and logs now
  report selected series range + counts.

- **OpenAI Symphony workflow setup** (2026-06-11) - Added the repo-owned
  `WORKFLOW.md` contract for the existing Linear `mib-studio` project, plus
  `scripts/start-symphony.ps1` to bootstrap the OpenAI Symphony Elixir
  reference implementation and run it against Codex app-server. Documented the
  trusted-environment assumptions, Linear project slug, workspace location, and
  startup path in `docs/howto/symphony.md`.

- **MindVision local SDK build enablement** (2026-06-11) - Reconfigured the
  Windows Debug build for `MIB_ENABLE_MINDVISION=ON` against the installed
  MindVision SDK layout, and fixed the SDK include handling so both
  `MindVision/CameraApiLoad.h` and flat `CameraApiLoad.h` installs compile.
  The SDK dynamic-loader symbols are now owned by `MindVisionCamera.cpp`, the
  Windows `max` macro no longer breaks the boot-time
  `MIB_CAMERA_MODE=mindvision` path, and hosted Windows GitHub workflows
  explicitly keep MindVision disabled because runners do not install the
  proprietary SDK. The release workflow now builds the default target set
  before `ctest` so test executables exist when release tests run; backend
  lifecycle tests now clean temporary directories only after backend teardown.
  The Overview/Experiment tab switch path now skips EGrabber JS script
  application when a MindVision camera is selected.
- **Remote-managed Young's modulus LUT** (2026-06-11) — Added a new
  `EModulusLutCatalog` backend helper that checks a public R2 manifest,
  downloads the LUT into a user-writable app-local cache, verifies the
  SHA-256 before activation, and preserves the bundled LUT as an automatic
  fallback when offline or incompatible. `AppBackend` now logs the active LUT
  source, revision, path, and checksum status at startup, the backend-only
  build links `QtNetwork`, and the repo gained `publish-emodulus-lut.py`,
  `verify-emodulus-lut-manifest.py`, and a backend smoke test covering remote
  update + fallback behavior. Docs were updated for the LUT R2 object layout
  and cache/rollback flow.

- **Public R2-backed profile catalog + manual updates** (2026-06-11) —
  `ConfigTabs` now uses a dedicated `ProfileManager` helper to scan local
  profiles, lazily create `profile.meta.json`, fetch public catalogs on
  demand, verify SHA-256 for staged downloads, back up the previous local
  profile files, install profile updates, and surface field-level config
  diffs plus local/remote/update state in the profile row. Bundled defaults
  gained `config_schema_version` so migrated configs stay self-describing.
- **Experiment config sync hardening** (2026-06-11) — `AppConfigWatcher`
  now writes back the full supported `image_processing` config section,
  including blur, background, auto-background, target-group emodulus, and
  `multi_image` fields, then emits `configFileChanged` immediately so the
  Preview JSON editor/table and Monitoring Tune Params refresh without
  waiting for filesystem-watcher timing. `ExperimentMonitoringTab` now
  updates the histogram ring-ratio defaults through the chart-range setter,
  and the shared `JsonFlatten` utility gained round-trip helpers plus tests
  covering nested objects, arrays of objects, arrays of scalars, root
  scalar tables, and the bundled `resources/defaults/config.json`.
- **MindVision camera SDK compatibility** (2026-06-11) - Added a separate
  MindVision camera backend and discovery path alongside the existing
  EGrabber and mock workflows. CMake now exposes `MIB_ENABLE_MINDVISION`
  and fails clearly when the SDK headers/runtime DLL are missing on Windows.
  The Connect tab has a dedicated MindVision selection path, `AppBackend`
  understands `MIB_CAMERA_MODE=mindvision`, and Windows packaging copies the
  MindVision runtime DLL when enabled. Backend build/tests and docs/vault notes
  were updated to keep the non-MindVision CI path green.

- **Conan remote health precheck workflow** (2026-06-10, _removed 2026-06-23_) — Added `.github/workflows/conan-remote-health.yml`, a scheduled/manual GitHub Actions health check for ConanCenter/team-remote reachability. Removed on 2026-06-23: it was failing persistently and not providing actionable signal; the release workflow already handles remote fallback (`conan install` retries without the team remote). The release-workflow.md preflight reference was dropped with it.
- **Cloudflare R2 CI publishing path cleanup** (2026-06-10) — Both
  Windows GitHub Actions release workflows now wire stable/beta publishing
  directly through `python publish-update.py`, map R2 credentials from
  `R2_ACCESS_KEY_ID`, `R2_SECRET_ACCESS_KEY`, and `MIB_STUDIO_R2_ENDPOINT`
  into S3 environment variables, and label Cloudflare R2 steps explicitly.
  Beta manifests now target `beta/latest.json`, while stable remains
  `stable/latest.json`. `docs/howto/release-workflow.md` and
  `docs/howto/auto-update-r2.md` were updated to document this channel naming
  and secret contract.

- **Long-run experiment backlog hardening** (2026-06-02) - `ProcessingService`
  now bounds the in-memory experiment frame backlog derived from the flush
  interval, drops sampled invalid frames before valid frames when HDF5 cannot
  keep up, and exposes count-only buffered-frame stats. `MainWindow` and
  `StatsDisplayManager` use the count API so 500 ms status polling no longer
  deep-copies full OpenCV frame payloads during long experiments.

- **Backend-only Linux build/test path** (2026-06-01) - Added
  `MIB_BUILD_BACKEND_ONLY` in `CMakeLists.txt` so backend workflows can skip
  frontend executable generation. Added `linux-backend-only` configure/build/test
  presets and a `mib_backend_smoke_test` CTest target (`backend` label) for
  backend-only verification loops. Updated Linux build docs and build/run vault
  notes accordingly.

- **Crash-resilient HDF5 checkpoints for experiment/recording writes**
  (2026-06-01, **superseded 2026-06-26**) - `Hdf5Service` originally flushed
  after each append/metadata write and copied a rolling recovery snapshot to
  `<file>.recovery.h5`, with `loadFile()` falling back to it. The `.recovery.h5`
  copy was removed in the 2026-06-26 finalization hardening above (it dominated
  NAS save time and starved the recorder thread); flushing is now time-interval
  based with no sidecar. Still current from this change: recording metadata
  writes open/create existing groups/attributes instead of failing on reruns,
  and `MainWindow::onStopExperiment` flushes before writing final experiment
  metadata.

- **GUI-configurable boot disable list** (2026-05-22) - Added a Settings menu
  action (**Boot Service Toggles...**) that persists disabled startup services
  in `QSettings` (`Startup/DisabledServices`). `main.cpp` now applies that
  persisted value to `MIB_DISABLED_SERVICES` before `AppBackend::initialize`,
  so GUI choices take effect at next launch. `MainWindow` also honors
  `auto_update` at startup by skipping updater initialization and quiet checks
  when disabled. Files: `src/frontend/core/main.cpp`,
  `src/frontend/core/MainWindow.cpp`.

- **Sentry build-pipeline wiring** (2026-05-22, same branch) — Wired
  the Sentry DSN end-to-end so installed builds report crashes without
  per-machine setup. CMake gained `MIB_SENTRY_DSN` and
  `MIB_SENTRY_ENVIRONMENT` cache vars that get forwarded to ISCC; both
  `mib-studio-qt.iss` and `mib-studio-qt-update.iss` now ship
  `crashpad_handler.exe` and emit a `[Registry]` entry writing
  `HKLM\…\Environment\MIB_SENTRY_DSN` (cleanly removed on uninstall).
  `.github/workflows/build-windows.yml` injects the DSN at configure
  time from the `SENTRY_DSN` repo secret, verifies the build produced
  `mib_studio_qt.pdb` + `crashpad_handler.exe`, and runs
  `sentry-cli debug-files upload` + `sentry-cli releases new/finalize`
  using `SENTRY_AUTH_TOKEN`/`SENTRY_URL`/`SENTRY_ORG`/`SENTRY_PROJECT`
  (skips cleanly when the auth token is absent). The setup is
  documented for operators in `docs/howto/sentry-setup.md`, with the
  troubleshooting guide updated to point at the new structured crash
  artifacts under `%LOCALAPPDATA%/MIB_Studio_Qt/crashes/`.

- **Crash monitoring + remote logging** (2026-05-22, branch
  `claude/crash-monitoring-logging-jUziR`) — Added a process-level crash
  pipeline that captures Windows minidumps and a JSON snapshot of live
  service state on any unrecoverable failure (SEH, signals, uncaught C++
  exceptions, Qt fatal). New [[../services/CrashReporter]] installs the
  handlers via `dbghelp` / `std::signal` / `std::set_terminate` /
  `qInstallMessageHandler` and optionally forwards events to Sentry via
  `sentry-native` (CMake-managed clone, off by default if the fetch fails or
  `MIB_USE_SENTRY=OFF`). New [[../diagnostics/CrashStateMirror]] gives
  every service a lock-free atomic slot so the crash handler can read
  state without taking any locks; `CaptureService`, `ProcessingService`,
  `Hdf5Service`, `FrameStore`, `AutofocusService`, and `AppBackend`
  recording all write to their slots at existing lifecycle hot-spots.
  CMake now emits `/Zi + /DEBUG /OPT:REF /OPT:ICF` for Release builds so
  the produced `mib_studio_qt.pdb` can be archived for later
  symbolication via `sentry-cli upload-dif`. Crash dumps land under
  `%LOCALAPPDATA%/MIB_Studio_Qt/crashes/` and (when a DSN is configured
  via the `MIB_SENTRY_DSN` env var) pending dumps from prior runs are
  drained on next launch. Files: new `CrashReporter.{h,cpp}`,
  `CrashStateMirror.{h,cpp}`, `cmake/Sentry.cmake`; modified
  `CMakeLists.txt`, `src/frontend/core/main.cpp`,
  `src/backend/AppBackend.cpp`, and the five services above.

- **Recording HDF5 mask regeneration in Review** (2026-05-11) - The
  Review tab now keeps "Regenerate Masks" enabled for recording-mode HDF5
  files. `BatchMaskDialog` resolves the active HDF5 source dataset and uses
  `/recorded_frames/images` for recording files instead of the hardcoded
  `/valid_frames/images`, so generated recording `.h5` files can be
  remasked and reloaded as standard review HDF5 outputs. The dialog also
  has a whole-HDF5 option that processes all recording frames or both
  valid/invalid datasets for standard files, preserving source frame indices
  and writing timestamps normalised to the first regenerated image. When no
  manual background frame is selected, regeneration can synthesize a
  background by averaging the lowest-change source frames per image tile.
  Files: `HdfReviewTab.cpp`, `BatchMaskDialog.{h,cpp}`.

- **Release windeployqt Conan alignment** (2026-04-20) — `CMakeLists.txt`
  picks `windeployqt` and per-config `PATH` from `qt_PACKAGE_FOLDER_DEBUG` /
  `qt_PACKAGE_FOLDER_RELEASE` (CMakeDeps) instead of a cached `find_program`
  result and non-deterministic Conan-cache globs, fixing MSB3073 when those
  pointed at a different Qt package than the one linked for Release.

- **Recording-mode HDF5 files now open in the Review tab** (2026-04-19) —
  The "Record" button (PlaybackPanel → `AppBackend::startFrameRecording`)
  writes raw frames to `/recorded_frames/{images,metadata}` with a
  `/recording_info` group; the Review tab was hardcoded to
  `/valid_frames/*` and `/invalid_frames/*` and showed nothing.
  [[../services/Hdf5Service]] grew three reader APIs: `isRecordingFile()`,
  `readRecordingMetadata(frames)`, `readRecordingInfo(start, end, total,
  filtered)`. [[../frontend/HdfReviewTab]] now detects recording files on
  load, hides the "Invalid Frames" tab, relabels "Valid Frames" as
  "Frames", and routes all thumbnail/viewer/export reads through new
  `imagesPath(bool)` / `masksPath(bool)` helpers. Masks, overlay modes,
  ROI overlay, Export Metrics CSV and Export Charts are disabled for
  recording files (no per-frame metrics exist); Export All still writes
  TIFFs. Regenerate Masks was re-enabled on 2026-05-11. Files:
  `Hdf5Service.{h,cpp}`, `HdfReviewTab.{h,cpp}`.

- **Buffer save to AVI + AVI source for mask regeneration** (2026-04-16) —
  [[../data-model/FrameStore]] gained `saveFramesToAvi()` overloads
  (all/index-range/timestamp-range) writing a single uncompressed AVI via
  `cv::VideoWriter` — Y800 preferred, DIB/BGR fallback. Y800 files don't
  play in Windows Media Player / Movies & TV (known codec support gap)
  but play in VLC and round-trip cleanly through `cv::VideoCapture` and
  ImageJ.
  [[../frontend/Dialogs]] `BufferSaveDialog` adds an "Output Format" radio
  group — **AVI is the default** — with FPS spinner; the dialog
  auto-iterates the output path (`_1`, `_2`, ...) so it never overwrites
  an existing file or non-empty folder, and after an AVI save the
  confirmation dialog tells the user they can view the file with ImageJ
  or Fiji (no auto-launcher). [[../services/BatchMaskSources]] gets
  `loadFromAvi()` and `BatchMaskDialog` grows a third "AVI video file"
  source radio. `CMakeLists.txt` links `opencv_videoio` on both targets.
  Files: `CMakeLists.txt`, `FrameStore.{h,cpp}`, `PlaybackService.{h,cpp}`,
  `BufferSaveDialog.{h,cpp,ui}`, `BatchMaskSources.{h,cpp}`,
  `BatchMaskDialog.{h,cpp}`.

- **Periodic sort-trigger test button** (2026-04-16) — Added
  `periodicTriggerBtn` (checkable) + `periodicTriggerIntervalSpin` to
  the top row of [[../frontend/ExperimentMonitoringTab]]. When armed,
  a `QTimer` fires [[../services/TriggerService]]::`onTargetGroupResult(true)`
  every N ms (10..60000, default 1000). Interval spinbox locks while
  armed; `hideEvent` disarms. Pure UI addition; no backend changes.
  Files: `resources/ui/ExperimentMonitoringTab.ui`,
  `include/frontend/tabs/ExperimentMonitoringTab.h`,
  `src/frontend/tabs/ExperimentMonitoringTab.cpp`.

- **BatchMaskDialog always saves standard HDF5** (2026-04-16) —
  Replaced the Output group box (Display / Save PNG / Save HDF5 checkboxes)
  with a single auto-save path: `<source_dir>/<stem>_remasked.h5`. After Run,
  `HdfReviewTab` reloads via `loadHdfFile()` giving full scatter plot,
  histogram, metadata table, and thumbnail support. Overwrite is prompted.
  Files: `BatchMaskDialog.h/cpp`, `HdfReviewTab.cpp`.

- **ROI & background selection GUI in BatchMaskDialog** (2026-04-16) —
  `BatchMaskDialog` extended with a right-hand preview panel. New
  `RoiDrawCanvas` widget (`src/frontend/utils/RoiDrawCanvas.cpp`) renders a
  source frame and accepts drag-to-draw ROI selection. Frame nav buttons
  (←/→) lazy-load frames one at a time from HDF5 or folder; "Set as
  Background" captures the current frame as the subtraction background.
  ROI pre-populates from HDF5 `experiment_info` on open. `onRun()` now
  uses dialog-selected ROI + background instead of live pipeline values.

- **Batch mask generation from stream images** (2026-04-15) —
  [[../services/ProcessingService]] gained `computeProcessedFrame()` and
  `processBatch()`, enabling offline mask regeneration without driving the
  realtime loop. New [[../services/BatchMaskSources]] adapters load from
  HDF5 / folder and save as PNG / HDF5. [[../frontend/HdfReviewTab]] gets
  a "Regenerate masks…" toolbar button backed by `BatchMaskDialog`.
- **Dual syringe pump control** (PR #58) —
  [[../services/SyringePumpService]] + [[../frontend/SyringePumpTab]] +
  `SyringePumpSettingsDialog`. Modbus RTU over two COM ports (Sample +
  Sheath).
- **Multi-image recording mode** (PR #57) — [[../services/ProcessingService]]
  `multi_image_enabled`, `ProcessedFrame::seriesImages`. HDF5 gets a
  4D `series_images` dataset. [[../frontend/HdfReviewTab]] grew
  `readSeriesImagesByIndex` support.
- **Parameter tuning + monitor overlay** (PR #55) — bidirectional sync
  between the param-tuning panel and the config table; see
  [[../frontend/ConfigTabs]].
- **Ring ratio configuration and validation** — added
  `ring_ratio_min/max` + `enable_ring_ratio_check` to
  `ProcessingConfig`; [[../services/AutofocusService]] consumes the
  same values via callback.
- **Review-tab crash fix + Close File button** (PR #61) — releases HDF5
  handles cleanly; see [[../frontend/HdfReviewTab]].

## Recent fixes

- **2026-06-24** — Fixed `hardware.camera` test (`tests/hardware/hw_camera_test.cpp`)
  which asserted `isCameraConfigured()` immediately after `initialize()` with
  `MIB_CAMERA_MODE=hardware`. The EGrabber boot path (`AppBackend.cpp` ~545)
  installs the camera factory but intentionally leaves the device *selection* to
  the connect flow (so `ConnectTab` can still run discovery and pick a device),
  so `isCameraConfigured()` was `false` and the test failed before any capture.
  The test now mirrors the connect flow — when not already configured it calls
  `setHardwareCameraSelection(MIB_TEST_EGRABBER_IF, MIB_TEST_EGRABBER_DEV,
  "egrabber")` (default 0/0), matching the sibling `hardware.egrabber_script`
  test. MindVision mode is unaffected (it records its selection at boot).
  Verified on-device: captured 61 frames from an SVS-VISTEK EoSens2.0MCX12.
  Backend behaviour was deliberately left unchanged to avoid making boot-time
  hardware mode skip `ConnectTab` discovery on multi-camera rigs.

- **2026-05-05** — Made `scripts/hdf5_export.spec` and
  `scripts/build_mac.sh` portable for Unix packaging of the HDF5 Export GUI.
  `hdf5_export.spec` now resolves its script directory robustly across
  PyInstaller execution contexts (`__file__`, `SPECPATH`, fallback cwd), so
  invoking from repo root (`pyinstaller scripts/hdf5_export.spec`) works.
  `build_mac.sh` now supports both macOS and Linux: Linux builds produce
  `scripts/dist/hdf5_export_app` (ELF), while macOS still produces
  `scripts/dist/hdf5_export_app.app` with optional `--dmg`. The script also
  handles environments missing `python3-venv` by falling back to system
  Python, avoids pip self-upgrade on distro-managed Python, validates existing
  `.venv` usability, and retries dependency install in a Linux-safe way.
  Validation in cloud: `bash scripts/build_mac.sh --clean` succeeded on Linux,
  and `python3 -m PyInstaller scripts/hdf5_export.spec ...` from repo root also
  succeeded.

- **2026-04-28** — Syringe Pump Settings now supports per-pump Modbus
  baud/address configuration and in-dialog address scanning. Added
  `SyringePumpService::scanModbusAddresses(...)` and wired Sample/Sheath scan
  controls in `SyringePumpSettingsDialog` so discovered addresses can be
  applied directly and persisted to config.
- **2026-04-28** — Added a standalone raw-serial Modbus helper script for
  dLSP501 pump bring-up and manual control:
  `scripts/dlsp501_pump_minimal.py` (+ unit tests in
  `scripts/test_dlsp501_pump_minimal.py`). The script uses pyserial only
  (no higher-level Modbus package) and exposes minimal commands for
  enable/start/stop, flow+direction setup, purge, and status reads.
- **2026-04-20** — Removed obsolete `capture_processing_test` from the build.
  `CMakeLists.txt` no longer defines a `BUILD_TESTING` block for the deleted
  `src/tests/capture_processing_test.cpp` harness, and docs/vault notes were
  updated so run/build guidance now lists only `mib_studio_qt` and
  `mock_studio_qt`.
- **2026-04-20** — Made ONNX Runtime optional for Linux/cloud configure paths.
  `CMakeLists.txt` now uses `find_package(onnxruntime CONFIG QUIET)`, sets
  `MIB_HAS_ONNXRUNTIME`, and compiles `YoloService.cpp` only when the
  `onnxruntime::onnxruntime` target exists; otherwise it compiles
  `src/backend/services/YoloService.stub.cpp`. This preserves startup behavior
  (backend continues when YOLO is unavailable) while unblocking cloud builds in
  environments without packaged ONNX Runtime CMake config files.
- **2026-04-20** — Documented Linux cloud linker/toolchain workaround for
  `cannot find -lstdc++` in
  `docs/howto/mock-camera-dev-mode.md` and
  [[../build-and-run/Build]]. Root cause in affected images: `c++`
  alternative pointed to clang pathing that failed to resolve an unversioned
  `libstdc++.so` during link. Workaround:
  `sudo update-alternatives --set c++ /usr/bin/g++`, then rerun CMake.
  Validation in cloud: linker error cleared; subsequent failures were dependency
  provisioning / Conan graph issues (not compiler runtime linking).
- **2026-04-20** — Guarded Windows-only hardware SDK dependencies so Linux
  cloud builds can still compile non-hardware code paths
  (`cursor/guard-windows-deps-linux-build-fb9e`). `CMakeLists.txt` now sets
  `MIB_HAS_EGRABBER` (`ON` on Windows, `OFF` elsewhere), gates EGrabber/Coremor
  include+link paths, and compiles `AutofocusService.cpp` only on Windows
  (with `AutofocusService.stub.cpp` on non-Windows). Runtime paths now default
  to mock-camera behavior when hardware SDKs are unavailable:
  `AppBackend` forces mock mode on non-Windows and `CaptureService` default
  factory uses `MockCamera`. `CameraControlService` and `EGrabberCamera` gained
  non-Windows stubs so Linux builds no longer require EGrabber headers/libs.
  Task record:
  `knowledge_map/task/2026-04-20-linux-build-windows-hardware-guards.md`.
- **2026-04-16** — Moved autofocus statistics sort onto its own thread
  (`claude/audit-thread-performance-Pr9OI`). Follow-up to the callback
  reorder below: instead of just running ring-ratio second on the
  realtime thread, the sort is now off the realtime thread entirely.
  `AutofocusService::onRingRatio` is O(1) — a push into
  `pendingSamples_` + atomic freshness markers + `notify_one`. A new
  `statsThread_` (lifetime = service constructor → destructor) drains
  the inbox at up to 100 Hz, maintains the 1000-sample deque under
  `ringRatioMutex_`, and refreshes the `{median, average, min, max}`
  atomics. The ProcessingService realtime thread no longer touches
  `ringRatioMutex_` or the sort. Post-step buffer clear in `controlLoop`
  now also clears `pendingSamples_` under a combined `std::scoped_lock`
  so pre-step samples don't leak forward.
- **2026-04-16** — Thread performance audit + trigger callback reorder
  (`claude/audit-thread-performance-Pr9OI`). Swept every long-running
  thread for UI-thread coupling to the trigger path; the 2026-04-15 fix
  (callbacks hoisted above `monitoringFramesMutex_`) holds. Remaining
  hot-path issue: within the hoisted block, `RingRatioCallback` fired
  **before** `TargetGroupCallback`, so the [[../services/TriggerService]]
  CV wake-up was serialised behind `AutofocusService::onRingRatio`, which
  locked `ringRatioMutex_` and ran an O(n log n) sort over up to 1000
  samples (~20–50 µs per valid frame). Reordered so target-group fires
  first in all three realtime paths (ROI+drop, full+drop, every-frame).
  Task record: `knowledge_map/task/2026-04-16-thread-performance-audit.md`.
- **2026-04-15** — Trigger onset latency regression fix
  (`claude/fix-trigger-timing-bug-xGgbx`). The target-group callback
  (which wakes [[../services/TriggerService]]) was being dispatched inside
  `monitoringFramesMutex_`, the same mutex held by the UI thread when it
  snapshotted the 1000-frame monitoring ring buffer for
  [[../frontend/ExperimentMonitoringTab]] (every 500 ms). End-to-end
  trigger onset drifted from ~400 µs to up to ~1 ms at the UI cadence. Now
  the callback fires before the monitoring mutex is taken. Also removed
  per-frame `cv::Mat::clone()` on `previousFrameForAutoCapture_` (shallow
  refcounted copy is enough). Task record:
  `knowledge_map/task/2026-04-15-trigger-timing-bug.md`.
- Param tuning panel now stays in sync with the config table both
  directions.
- HDF Review tab: fixed dangling pointer crash on file close.
- **2026-04-15** — `package_installer` / `package_installer_update`
  now pass `/O${INSTALLER_OUTPUT_DIR}` to ISCC so installers land in
  `build/dist/` as CMake and `build-windows.yml` expect. Without it,
  Inno Setup honored `OutputDir=build\dist` from the `.iss` file
  relative to `resources/installers/`, writing to
  `resources/installers/build/dist/` and breaking the CI artifact
  upload step.
- **2026-04-15** — `build-windows.yml` beta path now uses
  `gh release create --target <sha>` instead of `git tag` + create.
  The local tag was never pushed, so `gh release create` refused to
  bind the release to it. `--target` makes gh create the tag
  server-side atomically.
- **2026-04-15** — `publish-update.ps1` switched from `aws` CLI to a
  small boto3 helper at `scripts/s3_upload.py`. Both CLIs omit
  `Content-Length` on `CreateMultipartUpload` and s3.yofo.bio
  rejects that; the helper registers a `before-send.s3` event hook
  that forces `Content-Length` onto every S3 request before it goes
  out. Workflows now `pip install conan boto3`. Local runs also
  require boto3 (`pip install boto3`).

## Historical tasks worth knowing about

See [[Task-Log-Index]] for the full list. Highlights:

- Safe start/stop of EGrabber (`-1012` shutdown errors).
- Preview capped at 60 Hz (configurable).
- Nanopositioner tab moved into the Config area.
- Live config reload propagates to all services.
- Config profiles (Save/Load per-user settings) — planned.
- Review system scalable to 2 GB files (lazy reads + virtualization).

## Branch context

Active development branches use the `claude/` prefix (e.g.
`claude/create-agent-onboarding-docs-J9j66`). Main is the integration
branch.
