# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Onboarding (read this first)

New agents should ramp up via the knowledge vault before making changes. Start at
`knowledge_map/README.md` and follow the `[[WikiLinks]]`. For a guided reading
order, open `knowledge_map/Agent-Onboarding.md`.

The vault (Obsidian-style, atomic notes) covers:
- **Architecture** — layered design, `AppBackend` composition root, threading, data flow
- **Services** — one note per backend service under `knowledge_map/services/`
- **Frontend** — one note per tab/controller under `knowledge_map/frontend/`
- **Camera abstraction**, **FrameStore**, **HDF5 storage**
- **Domain glossary** (deformability, ring ratio, PFNC, GenICam, etc.)
- **Build & run**, **conventions**, **current state**

Related locations (unchanged):
- `docs/` — user-facing how-tos and integration guides
- `knowledge_map/task/` — dated task records (historical context)

## Build Commands

```bash
# Configure (Windows, VS2022 x64, Conan toolchain)
cmake --preset windows-default

# Build Debug
cmake --build build --config Debug

# Build Release
cmake --build build --preset windows-default-build-release

# Deploy (auto-triggered by CMake post-build, but can run manually)
windeployqt.exe --release build/Release/mib_studio_qt.exe
```

## Running

- `mib_studio_qt.exe` — Production app (hardware camera)
- `mock_studio_qt.exe` — Development app with GUI mock camera selector
- `capture_processing_test.exe` — Console test harness

### Mock Camera Environment Variables

- `MIB_CAMERA_MODE=mock` — Use folder-backed mock camera
- `MIB_MOCK_CAMERA_DIR=<path>` — Folder of TIFF/PNG/JPEG images
- `MIB_MOCK_CAMERA_INTERVAL_MS=<ms>` — Frame interval (default 33)
- `MIB_MOCK_CAMERA_LOOP=true|false` — Loop or stop at end

## Architecture

C++17 / Qt6 (Widgets + Charts) application for real-time microscopy image capture, processing, and analysis. Uses OpenCV for image processing, HDF5 for experiment data storage, and Euresys EGrabber SDK for hardware camera integration.

### Layered Design

**Frontend** (`src/frontend/`) — Qt widgets. `MainWindow` coordinates tabs: `ConnectTab` (device selection), `PreviewPage` (live display + `PlaybackPanel`), `ConfigTabs` (processing + camera-script config), `ExperimentMonitoringTab` (live histograms), `HdfReviewTab` (post-experiment review), `NanopositionerTab` (autofocus), `SyringePumpTab` (dual-pump control).

**AppBackend** (`src/backend/AppBackend.cpp`) — Service facade. Owns and wires all backend services. Entry point for all backend operations.

**Services** (`src/backend/services/`) — Each service owns a single concern:
- `CaptureService` — Streams frames from camera into ring buffer on a dedicated thread
- `ProcessingService` — Image analysis via worker thread pool; classifies frames valid/invalid by deformability, area, brightness
- `Hdf5Service` — Batched writes to HDF5 files (PIMPL pattern hides HDF5 details)
- `PlaybackService` — Wraps `FrameStore` ring buffer for UI queries
- `AutofocusService` — Nanopositioner control via serial COM port; uses ring ratio feedback from processing
- `CameraControlService` — GenICam parameter application
- `TriggerService` — Camera digital output pulse on target-group frame detection
- `SyringePumpService` — Dual-pump (sample/sheath) Modbus control over serial
- `YoloService` — ONNX Runtime model loader (segmentation; optional)
- `RecorderService` — Raw frame container writer (recording mode)
- `SqliteService` — Metadata persistence

**Camera abstraction** (`src/camera/`) — `ICamera` interface with `EGrabberCamera` (hardware) and `MockCamera` (folder-backed) implementations.

**FrameStore** (`src/backend/playback/FrameStore.cpp`) — Ring buffer for in-memory frame history. Default constructor capacity is 512, but `AppBackend::initialize` overrides it to **5000**.

### Threading Model

- **Main thread**: Qt event loop (UI)
- **Capture thread**: `CaptureService::run()` blocks on `camera->grabFrame()`
- **Processing workers**: Thread pool in `ProcessingService` (default = hardware concurrency)
- **Realtime processing**: Dedicated async loop for low-latency analysis
- **Autofocus**: Dedicated thread for serial COM communication

### Data Flow

1. Camera → `CaptureService` → `FrameStore` (ring buffer) + callback to UI
2. Frames → `ProcessingService::realtimeLoop()` → valid/invalid classification
3. Ring ratio → `AutofocusService` for nanopositioner feedback
4. Valid/invalid frames batched → `Hdf5Service::appendFrames()` for persistent storage

## Key Conventions

- Use **spdlog** for logging; never `std::cout` in app code
- Reference `egrabber-sample-programs` before implementing camera functions; prefer ready-made SDK patterns
- Review existing `Tools` (`src/backend/Tools.cpp`) before writing new utilities
- Refresh `StreamModule` counters before stopping capture for accurate shutdown stats
- Tasks/issues go in `knowledge_map/task/`; docs go in `docs/`
- Headers mirror source layout under `include/`
- `include/Coremor/` contains third-party nanopositioner DLL (checked into repo)
- Runtime data (logs, sqlite, HDF5 files, mock frames) lives under `data/`

## Test Performance Tracking

When running test performances, save all metrics (including intermediates) to MLflow at `mlflow.yofo.bio`. Set credentials via environment variables `MLFLOW_TRACKING_USERNAME` and `MLFLOW_TRACKING_PASSWORD` — do not hardcode them.
