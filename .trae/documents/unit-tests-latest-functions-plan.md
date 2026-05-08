## Summary
- Add a C++ unit test target for the backend library using Catch2 (via CMake FetchContent).
- Implement unit tests covering the most recently added/changed “Recent Work” APIs:
  - `backend::services::Hdf5Service`: `isRecordingFile()`, `readRecordingMetadata(...)`, `readRecordingInfo(...)`
  - `backend::services::ProcessingService`: `computeProcessedFrame(...)`, `processBatch(...)`
  - `backend::playback::FrameStore`: `saveFramesToAvi(...)` (validation + range behavior; keep actual codec write-path as an optional integration-style assertion)

## Current State Analysis
- The project is primarily C++ (Qt6) with a backend static library target: `mib_backend` defined in [CMakeLists.txt](file:///workspace/CMakeLists.txt).
- There is no active C++ test target wired into CMake; prior `BUILD_TESTING` content was removed (notably, an older `src/tests/capture_processing_test.cpp` harness).
- Existing tests in the repo are limited to a Python script test: [test_dlsp501_pump_minimal.py](file:///workspace/scripts/test_dlsp501_pump_minimal.py).
- The “latest functions” to test are identified from [Recent-Work.md](file:///workspace/knowledge_map/current-state/Recent-Work.md) and map to:
  - Recording-mode HDF5 readers in [Hdf5Service.h](file:///workspace/include/backend/services/Hdf5Service.h) and [Hdf5Service.cpp](file:///workspace/src/backend/services/Hdf5Service.cpp).
  - Offline/batch processing pipeline functions in [ProcessingService.h](file:///workspace/include/backend/services/ProcessingService.h) and [ProcessingService.cpp](file:///workspace/src/backend/services/ProcessingService.cpp).
  - AVI export helpers in [FrameStore.h](file:///workspace/include/backend/playback/FrameStore.h) and [FrameStore.cpp](file:///workspace/src/backend/playback/FrameStore.cpp).

## Proposed Changes
### 1) Add a CMake test entrypoint (Catch2 via FetchContent)
- **File**: [CMakeLists.txt](file:///workspace/CMakeLists.txt)
  - Add `include(CTest)` (or `enable_testing()` + option `BUILD_TESTING`) so tests are opt-in.
  - Add `if(BUILD_TESTING) add_subdirectory(tests) endif()` to keep tests out of production builds by default.
  - Keep all existing application/library targets unchanged when `BUILD_TESTING=OFF`.

- **New file**: `/workspace/tests/CMakeLists.txt`
  - Use `FetchContent_Declare` + `FetchContent_MakeAvailable` for Catch2.
  - Define a test executable, e.g. `mib_backend_tests`.
  - Link `mib_backend_tests` against:
    - `mib_backend` (covers OpenCV/HDF5/spdlog/Qt link requirements via existing target configuration)
    - `Catch2::Catch2WithMain` (preferred, to avoid maintaining a custom main)
  - Register tests with CTest:
    - Prefer Catch2’s CMake integration (`catch_discover_tests`) if available from the fetched Catch2 version.
    - Otherwise, register a single `add_test(NAME mib_backend_tests COMMAND mib_backend_tests)` as a fallback.

### 2) Add unit tests for `ProcessingService` batch pipeline
- **New file**: `/workspace/tests/backend/test_processing_service.cpp`
- **Coverage goals**
  - `computeProcessedFrame(...)`
    - Converts BGR input to grayscale (`originalImage` becomes `CV_8UC1`).
    - Produces a full-frame mask (`processedImage` same width/height as input) and keeps pixels outside ROI as 0.
    - Sets `index` and `timestampNs` correctly.
    - Produces deterministic validation for a synthetic blob by using a config tuned for the test:
      - Set `require_single_inner_contour=false` (so a simple filled contour is validatable).
      - Widen/disable thresholds as needed (e.g., `area_threshold_min=0`, `area_threshold_max` large; optionally disable ring-ratio check).
  - `processBatch(...)`
    - Returns the same number of frames as input.
    - Sets frame indices `0..N-1`.
    - Invokes the progress callback at least at start and end, with `(done,total)` consistent and monotonically increasing.

### 3) Add unit tests for `Hdf5Service` recording-mode readers
- **New file**: `/workspace/tests/backend/test_hdf5_service_recording.cpp`
- **Approach**
  - Use `std::filesystem::temp_directory_path()` and a unique per-test file name to avoid collisions.
  - Create a recording-mode file using the public API:
    - `openFile(tmpFile)`
    - `initializeRecordingDatasets()`
    - `appendRecordingFrames(images, metadata)` with a small set of synthetic `cv::Mat` frames and matching `RecordingFrameMeta` entries
    - `writeRecordingInfo(start,end,total,filtered)`
    - `closeFile()`
  - Re-open read-only via `loadFile(tmpFile)` and assert:
    - `isRecordingFile()` is `true`
    - `readRecordingMetadata(frames)` returns `true` and fills `ProcessedFrame::index` / `timestampNs` matching what was written (other fields remain default/empty)
    - `readRecordingInfo(...)` returns `true` and yields the written attributes
  - Add negative-path coverage:
    - A file without `/recording_info` returns `false` for `isRecordingFile()` and `readRecordingInfo(...)`.

### 4) Add unit tests for `FrameStore::saveFramesToAvi` (non-flaky core behavior)
- **New file**: `/workspace/tests/backend/test_frame_store_avi.cpp`
- **What to test reliably**
  - Empty store returns `false` for `saveFramesToAvi(outputPath, ...)`.
  - Invalid ranges:
    - `startIndex > endIndex` returns `false`.
    - Range outside available indices returns `false`.
  - Filtering behavior and codec-dependent write success will be treated as optional:
    - If `saveFramesToAvi(...)` returns `true`, assert the output file exists and is non-empty.
    - If it returns `false` due to `cv::VideoWriter` being unavailable in the environment, do not fail the test; instead, assert that the precondition checks were satisfied and treat the encode path as an environment-dependent integration capability.

## Assumptions & Decisions
- Unit tests will be introduced as opt-in via `-DBUILD_TESTING=ON` and will not affect default production builds.
- Catch2 will be fetched at configure time via CMake FetchContent (requires network access in the build environment).
- Tests will focus on backend code paths and avoid UI/Qt Widgets usage.
- AVI writer availability varies by OpenCV build; tests will avoid hard-failing on missing codecs while still covering FrameStore’s deterministic validation logic.

## Verification Steps
- Configure with tests enabled:
  - `cmake -S . -B build -DBUILD_TESTING=ON`
- Build:
  - `cmake --build build`
- Run tests:
  - `ctest --test-dir build --output-on-failure`
