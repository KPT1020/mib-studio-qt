# MockCamera

> Folder-backed [[ICamera]] for development and tests. Reads PNG/TIFF/JPEG
> images from a directory and replays them at a configured interval.

**Source:** `src/camera/mock/MockCamera.cpp`,
`include/camera/mock/MockCamera.h`
**Related:** [[ICamera]], [[../build-and-run/Run-Modes]]
(mock env vars), [[../frontend/ConnectTab]]

## Options — `MockCameraOptions`

```cpp
std::filesystem::path folder;
std::chrono::microseconds frameInterval{33'000}; // ~30 fps default
bool loopFiles{true};
```

## Env vars (read in `main.cpp` / `AppBackend`)

- `MIB_CAMERA_MODE=mock` — force mock (bypasses ConnectTab selection)
- `MIB_MOCK_CAMERA_DIR=<path>` — folder source
- `MIB_MOCK_CAMERA_INTERVAL_MS=<ms>` — overrides `frameInterval`
- `MIB_MOCK_CAMERA_LOOP=true|false` — overrides `loopFiles`

## Responsibility

- `refreshFileList()` scans the folder for supported extensions.
- `preloadFrames()` reads all images into `preloadedFrames_` at start
  (so `grabFrame` is fast and deterministic).
- `grabFrame(out)` returns the next preloaded frame, sleeping as needed
  to hit `frameInterval`. Returns false when `loopFiles == false` and
  the list is exhausted.

## Gotchas

- Mock camera does not support trigger output (`setTriggerOutput` returns
  false) — [[../services/TriggerService]] pulses become no-ops.
- Timestamps are synthesized from steady-clock deltas, not device ticks —
  useful for dev, not for absolute timing.
- See `docs/howto/mock-camera-dev-mode.md` and task
  `knowledge_map/task/mock_camera_dev_mode.md`.
- `data/mock_frames/frame_00000.tiff` is checked in as a minimal sample.
