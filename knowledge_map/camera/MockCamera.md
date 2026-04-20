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
- `MIB_MOCK_CAMERA_SPIN_THRESHOLD_US=<us>` — for intervals at or below this
  threshold, pacing uses pure spin (default `500`)
- `MIB_MOCK_CAMERA_PIN_CPU=<cpu_index>` — optionally pin mock capture thread to
  a specific CPU (Linux uses `pthread_setaffinity_np`)

## Responsibility

- `refreshFileList()` scans the folder for supported extensions.
- `preloadFrames()` reads all images into `preloadedFrames_` at start
  (so `grabFrame` is fast and deterministic). Since 2026-04-20 it decodes in
  parallel (up to `hardware_concurrency`) and preserves lexical frame order.
- `grabFrame(out)` returns the next preloaded frame, sleeping as needed
  to hit `frameInterval`. Since 2026-04-20, pacing is adaptive:
  - intervals `<= spinThreshold` use pure busy-spin for sub-millisecond
    deadline fidelity
  - longer intervals use cooperative staged wait (coarse sleep with short
    sleep/yield near the target).
  Returns false when `loopFiles == false` and the list is exhausted.

## Gotchas

- Mock camera does not support trigger output (`setTriggerOutput` returns
  false) — [[../services/TriggerService]] pulses become no-ops.
- Timestamps are synthesized from steady-clock deltas, not device ticks —
  useful for dev, not for absolute timing.
- In pure-spin mode, one CPU core is effectively dedicated to pacing; this is
  intentional for high-rate timing realism.
- See `docs/howto/mock-camera-dev-mode.md` and task
  `knowledge_map/task/mock_camera_dev_mode.md`.
- `data/mock_frames/frame_00000.tiff` is checked in as a minimal sample.
