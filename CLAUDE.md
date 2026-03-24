# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Install dependencies (Conan 2, requires profile: conan profile detect)
conan install . -of build --build=missing -s build_type=Release -s compiler.cppstd=17
conan install . -of build --build=missing -s build_type=Debug -s compiler.cppstd=17

# Configure (Windows, VS2022 x64, Conan toolchain)
cmake --preset windows-default

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --preset windows-default-build-release

# Run tests (30s timeout)
ctest --test-dir build --build-config Release --output-on-failure --timeout 30

# Build installers (requires InnoSetup 6)
cmake --build build --config Release --target package_installer
cmake --build build --config Release --target package_installer_update

# Deploy (auto-triggered by CMake post-build, but can run manually)
windeployqt.exe --release build/Release/mib_studio_qt.exe
```

## Running

- `mib_studio_qt.exe` — Production app (hardware camera)
- `capture_processing_test.exe` — Console test harness (mock camera mode)

## Environment Variables

| Variable | Purpose |
|---|---|
| `MIB_CAMERA_MODE=mock` | Use folder-backed mock camera |
| `MIB_MOCK_CAMERA_DIR=<path>` | Folder of TIFF/PNG/JPEG images |
| `MIB_MOCK_CAMERA_INTERVAL_MS=<ms>` | Frame interval (default 33) |
| `MIB_MOCK_CAMERA_LOOP=true\|false` | Loop or stop at end |
| `MIB_STUDIO_UPDATE_MANIFEST_URL` | Override auto-update manifest URL |
| `MLFLOW_TRACKING_USERNAME` / `MLFLOW_TRACKING_PASSWORD` | MLflow credentials for `mlflow.yofo.bio` — never hardcode |

## Architecture

C++17 / Qt6 (Widgets + Charts) application for real-time microscopy image capture, processing, and analysis. Uses OpenCV for image processing, HDF5 for experiment data storage, ONNX Runtime for YOLO inference, and Euresys EGrabber SDK for hardware camera integration.

### Dependencies (conanfile.txt)

`qt/6.7.3`, `spdlog/1.17.0`, `sqlite3/3.51.0`, `hdf5/1.14.6`, `opencv/4.12.0`, `onnxruntime/1.18.1`

### Layered Design

**Frontend** (`src/frontend/`) — Qt widgets organized by role:
- **Core** (`core/`): `MainWindow` coordinates tabs: `ConnectTab`, `PreviewPage`, `OverviewTab`, `ExperimentMonitoringTab`, `NanopositionerTab`, `HdfReviewTab`, `ConfigTabs`
- **Controllers** (`controllers/`): `CameraController` (start/stop capture with guard clauses), `ExperimentController` (experiment state machine: Idle→Starting→Active→Stopping)
- **System** (`system/`): `DeviceInitManager` (async device discovery with retry), `AppConfigWatcher` (live JSON config reload via QFileSystemWatcher), `AutoUpdater` (S3 manifest-based auto-update with SHA-256 verification), `PlaybackPanel`
- **Dialogs** (`dialogs/`): `BufferSaveDialog`, `ConversionFactorDialog`, `FrameViewerDialog`, `MockConfigDialog`, `MonitoringSettingsDialog`, `ProcessingSettingsDialog`
- **Utils** (`utils/`): Image rendering, ROI management, statistics display, config parsing, file I/O, JSON tools
- **Models** (`models/`): `HdfMetricsModel`, `JsonTableModel`

**AppBackend** (`src/backend/AppBackend.cpp`) — Service facade. Owns and wires all backend services. Entry point for all backend operations.

**Services** (`src/backend/services/`) — Each service owns a single concern:
- `CaptureService` — Streams frames from camera into ring buffer on a dedicated thread
- `ProcessingService` — Image analysis via worker thread pool; classifies frames valid/invalid by deformability, area, brightness
- `YoloService` — YOLO11 segmentation via ONNX Runtime (`yolo11n-seg.onnx`) for AI-based cell analysis
- `Hdf5Service` — Batched writes to HDF5 files (PIMPL pattern hides HDF5 details)
- `PlaybackService` — Wraps `FrameStore` ring buffer for UI queries
- `AutofocusService` — Nanopositioner control via serial COM port; uses ring ratio feedback from processing
- `CameraControlService` — GenICam parameter application and camera discovery
- `SqliteService` — Metadata persistence
- `RecorderService` — PIMPL stub for future frame recording
- `Logger` — Rotating file + console dual-sink logging (spdlog, 10MB max / 5 files)

**Camera abstraction** (`src/camera/`) — `ICamera` interface with `EGrabberCamera` (hardware) and `MockCamera` (folder-backed) implementations.

**FrameStore** (`src/backend/playback/FrameStore.cpp`) — Ring buffer (512 capacity) for in-memory frame history.

### Threading Model

- **Main thread**: Qt event loop (UI)
- **Capture thread**: `CaptureService::run()` blocks on `camera->grabFrame()`
- **Processing workers**: Thread pool in `ProcessingService` (default = hardware concurrency)
- **Realtime processing**: Dedicated async loop for low-latency analysis
- **Autofocus**: Dedicated thread for serial COM communication
- **Device discovery**: QtConcurrent worker threads in `DeviceInitManager`

### Data Flow

1. Camera → `CaptureService` → `FrameStore` (ring buffer) + callback to UI
2. Frames → `ProcessingService::realtimeLoop()` → valid/invalid classification
3. Ring ratio → `AutofocusService` for nanopositioner feedback
4. Valid/invalid frames batched → `Hdf5Service::appendFrames()` for persistent storage

### Configuration

`resources/defaults/config.json` ships default processing parameters, autofocus settings, and display options. `AppConfigWatcher` monitors a user-writable copy and hot-reloads changes into running services without restart. On Windows installs, the writable copy is at `%LOCALAPPDATA%/MIB_Studio_Qt/include/config.json`.

## CI/CD

Three GitHub Actions workflows in `.github/workflows/`:
- **`ci.yml`** — Runs on push to `main`/`develop` and PRs. Validates CMake config, InnoSetup scripts, and release script syntax.
- **`build-windows.yml`** — Manual dispatch. Builds beta or release with optional version bumping (patch/minor/major).
- **`release.yml`** — Triggered by `v*.*.*` tags. Full build → test → InnoSetup packaging → GitHub Release → RustFS publish for auto-updates.

### Release Scripts

- `bump-version.ps1` — Increments `DEFAULT_VERSION` in CMakeLists.txt
- `release.ps1` — Orchestrates local release workflow
- `publish-update.ps1` — Uploads update installer to RustFS and writes `latest.json` manifest
- `publish-tools.ps1` — Publishes Python analysis tools

## Python Scripts

`scripts/` contains post-processing and analysis tools:
- `reanalyse_hdf5.py` — Re-run processing pipeline on saved HDF5 data
- `export_hdf5.py` / `export_worker.py` / `hdf5_export_app.py` — Export HDF5 frames to images
- `compare_metrics.py` — Compare processing metrics across runs
- `empty_frame_detection.py` — Detect empty frames in datasets

## Resources

- `resources/ui/` — Qt Designer `.ui` form files (13 files)
- `resources/models/` — ONNX model (`yolo11n-seg.onnx`) and conversion script
- `resources/installers/` — InnoSetup `.iss` scripts (full installer + update package)
- `resources/defaults/` — Default `config.json`, `egrabberConfig.js`, `overviewConfig.js`

## Key Conventions

- Use **spdlog** for logging via `Logger::init()`; never `std::cout` in app code
- Reference `egrabber-sample-programs` before implementing camera functions; prefer ready-made SDK patterns
- Review existing `Tools` (`src/backend/Tools.cpp`) before writing new utilities
- Refresh `StreamModule` counters before stopping capture for accurate shutdown stats
- Tasks/issues go in `knowledge_map/task/`; docs go in `docs/`
- Headers mirror source layout under `include/`
- `include/Coremor/` contains third-party nanopositioner DLL (checked into repo)
- Runtime data (logs, sqlite, HDF5 files, mock frames) lives under `data/`

## Test Performance Tracking

When running test performances, save all metrics (including intermediates) to MLflow at `mlflow.yofo.bio`. Set credentials via environment variables — do not hardcode them.
