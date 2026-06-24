# AppBackend

> Composition root. Owns every backend service and the shared `FrameStore`.
> Frontend code holds a single `backend::AppBackend&` and calls getters.

**Source:** `src/backend/app/AppBackend.cpp`, `include/backend/app/AppBackend.h`
**Related:** [[Overview]], [[Data-Flow]], [[Threading-Model]],
[[../data-model/FrameStore]]

## What it owns

All services are `std::unique_ptr`; [[../data-model/FrameStore]] is
`std::shared_ptr` (shared with capture, processing, playback).

```cpp
sqliteService_, hdf5Service_,
captureService_, processingService_, playbackService_,
cameraControlService_, autofocusService_,
triggerService_, yoloService_, syringePumpService_
frameStore_  // shared_ptr<FrameStore>(5000)
```

## `initialize(dataDir)` — what it wires

See `src/backend/AppBackend.cpp` around lines 79–200.

1. Creates `dataDir` and resolves a user-writable log path (falls back to
   `%LOCALAPPDATA%/MIB_Studio_Qt/logs/app.log` on Windows when `dataDir`
   is under `Program Files`).
2. Instantiates all services + `FrameStore(5000)`.
3. `sqliteService_->initialize(dataDir/app.sqlite3)`,
   `hdf5Service_->initialize(dataDir)`.
4. Loads optional YOLO model from `resources/models/yolo11n-seg.onnx`.
5. Resolves the Young's modulus LUT through the managed R2/cache helper,
   preferring the user-writable copy under the app-local data tree and
   falling back to the bundled `resources/isoelastic_curve/...` file on
   first run, offline launches, or update failures.
6. Starts the processing worker pool (`processingService_->start()`).
   Realtime loop is **not** started here — it starts when the Experiment tab
   becomes active.
7. Wires callbacks:
   - `ProcessingService::RingRatioCallback` → `AutofocusService::onRingRatio`
   - `ProcessingService::TargetGroupCallback` → `TriggerService::onTargetGroupResult`
   - `CaptureService::CameraReadyCallback` → starts/stops `TriggerService`
     and hands it the live `ICamera*`
   - `ProcessingService::BackgroundCaptureCallback` → emits Qt signal via
     [[../frontend/System-Utilities]] `BackgroundCaptureNotifier`
8. Seeds the [[../diagnostics/CrashStateMirror]] with initial app context
   (camera label, data dir, mock vs hardware vs MindVision, FrameStore
  capacity) and sets the Sentry tags (`camera_mode`, `data_dir`) on
  [[../services/CrashReporter]].
  The reporter itself is initialized earlier in `main()`, before AppBackend
  exists.

### LUT management

The Young's modulus LUT now follows the same managed-asset model as the
profile catalog:

- `EModulusLutCatalog` checks `https://updates.yofo.bio/stable/emodulus-lut/latest.json`
  by default, with `MIB_STUDIO_EMODULUS_LUT_MANIFEST_URL` as an override.
- Remote payloads are verified with SHA-256 before replacing the local cache.
- The active LUT path is logged with source, revision, checksum status, and
  remote update outcome.
- `MIB_STUDIO_EMODULUS_LUT_CACHE_DIR` can redirect the cache path for tests
  or local validation.

### Boot-time service toggles (`MIB_DISABLED_SERVICES`)

`AppBackend::initialize` reads a comma-separated disable list from
`MIB_DISABLED_SERVICES` and conditionally skips selected startup wiring.

Supported backend tokens:

- `sqlite`
- `hdf5`
- `processing`
- `yolo`
- `autofocus` (disables ring-ratio callback wiring from processing)
- `trigger` (disables processing/camera trigger wiring)
- `capture` (alias: `camera`)
- `playback`
- `all` (disables all backend startup paths above)

Notes:

- Tokens are case-insensitive; `-` and `_` are treated the same.
- Services are still constructed to preserve existing references in frontend
  and backend code; toggles control startup wiring/initialization.

## Camera selection

- `setHardwareCameraSelection(ifIdx, devIdx, label)` — choose device (no start)
- `setMindVisionCameraSelection(cameraIndex, label)` — choose a MindVision
  device (no start)
- `configureMockCamera(options)` — choose mock folder instead
- `applyCameraScriptFromFile(path)` — push a GenICam JS config to the selected
  device (stops capture first, does not restart)
- `resetSelectedHardwareCamera()` — issue GenICam `DeviceReset`
- `applyMindVisionConfigFromFile(path)` — apply the selected MindVision JSON
  config and refresh the capture factory path

### Platform behavior

- On non-Windows builds, hardware camera initialization is forced to mock and
  logs a warning when a hardware mode is requested.
- On Windows, `MIB_CAMERA_MODE=mindvision` configures the MindVision capture
  factory when the build was configured with `MIB_ENABLE_MINDVISION=ON`; the
  startup parser clamps `MIB_MINDVISION_CAMERA_INDEX` to a non-negative index.
- `setHardwareCameraSelection()` becomes a guarded fallback on non-Windows:
  it logs a warning, switches the capture factory to `MockCamera`, clears
  selected hardware indices, and keeps `mockCameraConfigured_ = true`.
- `setMindVisionCameraSelection()` preserves the selected camera state even
  when the SDK is unavailable so the UI/backend selection remains explicit.

## Frame recording mode

Separate from experiments: record non-empty raw frames directly to HDF5 with
no contour processing.

- `startFrameRecording(hdf5Path)`, `stopFrameRecording()`, `isFrameRecording()`
- Counters: `frameRecordingCount()`, `frameRecordingFiltered()`
- Uses a dedicated `frameRecordingThread_` and leverages
  [[../data-model/FrameStore]]'s `setFrameFilter` to drop empty frames.
- The collector thread hands batches to a 3-slot [[../services/Hdf5Service]]
  `HdfWriteQueue` (writer thread does `appendRecordingFrames`), so slow disk no
  longer stalls FrameStore reads. The written count advances only on a confirmed
  write; a failed write or queue overflow stops recording and fires the fatal
  save-error sink. This fixed the old silent count-and-drop-on-failure bug.

### Fatal save-error sink

`setFatalSaveErrorCallback(cb)` / `reportFatalSaveError(msg)` funnel both
recording **and** experiment-flush ([[../services/ProcessingService]]) write
failures to one callback. `MainWindow` marshals it to the UI thread, stops the
active operation, and shows a modal Save Error dialog — failed saves are never
silent.

## Config JSON storage

`setLastConfigJson(json)` / `getLastConfigJson()` — raw JSON captured by the
config watcher, stored as a string attribute on `/experiment_info` in HDF5
(see `Hdf5Service::writeConfigJson`).
