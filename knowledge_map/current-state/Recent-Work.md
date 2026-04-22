# Recent Work

> Snapshot of recently merged features and fixes, as of 2025-11 / 2025-12.
> Refresh from `git log --oneline -20` when outdated.

## Features shipped

- **Standalone cross-platform `pump_control` app + modular multi-pump
  refactor** (2026-04-21) — The syringe-pump UI is now shippable as an
  independent binary (`pump_control.exe` on Windows; native binaries on
  Linux / macOS) that pulls in only Qt Widgets/SerialPort, spdlog, and
  nlohmann_json — no OpenCV / HDF5 / ONNX / SQLite. Along the way,
  [[../services/SyringePumpService]] was generalised from a hardcoded
  `{Sample, Sheath}` enum to a dynamic `std::map<PumpHandle,
  Pump>` so users can add and remove pumps at runtime. The new
  `QSerialPortInfo`-based port enumeration in `backend::Tools` and the
  `connect(QString portName, …)` overload on the service replace the
  Windows-only `"COM%d"` code path. `SyringePumpTab` now hosts a scroll
  list of `PumpRowWidget` instances with `+ Add Pump` / Remove buttons;
  `SyringePumpSettingsDialog` mirrors the same shape and auto-assigns a
  distinct serial port to each new row. Config schema migrated from
  flat `pump_sample_*` / `pump_sheath_*` keys to a `pumps: [{name,
  port_name, …}]` array (legacy keys are auto-migrated on first load).
  Files: `SyringePumpService.{h,cpp}`, `Tools.{h,cpp}`,
  `SyringePumpTab.{h,cpp}`, `PumpRowWidget.{h,cpp}`,
  `SyringePumpSettingsDialog.{h,cpp}`, `MainWindow.cpp`,
  `SidebarWidget.cpp`, new `src/standalone/pump_control/*`, `CMakeLists.txt`.

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
  ROI overlay, Regenerate Masks, Export Metrics CSV and Export Charts are
  disabled for recording files (no per-frame metrics exist); Export All
  still writes TIFFs. Files: `Hdf5Service.{h,cpp}`, `HdfReviewTab.{h,cpp}`.

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
