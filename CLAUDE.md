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

## Vault Maintenance (required for every change)

The vault is only useful if it stays current. **Every agent that modifies
code must update the vault in the same commit/PR as the code change.**

### When to update

| If you change... | Update the note at... |
|---|---|
| A service under `src/backend/services/<Name>Service.{cpp,h}` | `knowledge_map/services/<Name>Service.md` |
| A frontend tab / controller / dialog under `src/frontend/` | The matching note under `knowledge_map/frontend/` |
| `src/backend/AppBackend.{cpp,h}` wiring | `knowledge_map/architecture/AppBackend.md` |
| Threading, data flow, or layering | `knowledge_map/architecture/{Threading-Model,Data-Flow,Overview}.md` |
| `src/backend/playback/FrameStore.*` | `knowledge_map/data-model/FrameStore.md` |
| HDF5 schema / dataset paths (`Hdf5Service.cpp`) | `knowledge_map/data-model/HDF5-Storage.md` + `services/Hdf5Service.md` |
| `src/backend/camera/` (ICamera, EGrabber, Mock) | `knowledge_map/camera/*.md` |
| `CMakeLists.txt`, `conanfile.txt`, `CMakePresets.json` | `knowledge_map/build-and-run/{Build,Dependencies,Run-Modes}.md` |
| Conventions / logging patterns | `knowledge_map/conventions/*.md` |
| Domain vocabulary (new metric, new concept) | `knowledge_map/domain/{Glossary,Microscopy-Pipeline}.md` |
| **Added a new** service / tab / dialog / camera impl | Create the atomic note AND add it to the cluster's `_MOC.md` AND link it from `knowledge_map/README.md` and `knowledge_map/Agent-Onboarding.md` |
| **Renamed or removed** any of the above | Rename/remove the note AND update every `[[WikiLink]]` that points to it |

### What to update inside a note

- Change the **Responsibility** paragraph if behavior shifted.
- Update **Key APIs / Entry points** when public signatures change.
- Add new gotchas to **Gotchas**; remove fixed ones.
- Touch **Source:** paths if files moved.
- Update the module's `_MOC.md` if a new sibling concept appeared.

### Shipping the change

1. On every non-trivial feature/fix, also append a short dated entry to
   `knowledge_map/current-state/Recent-Work.md` (and — if the work was
   multi-step — create `knowledge_map/task/YYYY-MM-DD-<slug>.md`).
2. Before committing, verify that no wikilinks you touched are broken:
   `grep -r '\[\[' knowledge_map/` and confirm each target note exists.
3. If you find any note that disagrees with current code, fix it while
   you're there — the vault is a living document, not an archive.

Do not skip this step. If a reviewer sees code changes without matching
vault updates, they should push back.

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

**Camera abstraction** (`src/backend/camera/`) — `ICamera` interface with `EGrabberCamera` (hardware) and `MockCamera` (folder-backed) implementations.

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
