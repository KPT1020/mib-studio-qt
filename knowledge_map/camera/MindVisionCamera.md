# MindVisionCamera

> MindVision SDK-backed `ICamera` implementation. Used when the user selects a
> MindVision device and the build was configured with `MIB_ENABLE_MINDVISION=ON`.

**Source:** `src/backend/camera/mindvision/MindVisionCamera.cpp`,
`include/backend/camera/mindvision/MindVisionCamera.h`,
`include/backend/camera/mindvision/MindVisionConfig.h` (pure JSON parse + bounds)
**Tests:** `tests/backend/mindvision_config_test.cpp`
**Related:** [[ICamera]], [[../services/CameraControlService]],
[[../architecture/AppBackend]], [[../frontend/ConnectTab]]

## Responsibility

- Own the MindVision camera lifecycle: open, start, grab frame, stop, and
  release the SDK handle.
- Apply an optional JSON config file before capture starts.
- Expose trigger-output helpers used by the backend camera wiring.

## Key APIs

- `MindVisionCamera(int cameraIndex, std::string configPath = {})`
- `applyConfig(const CameraConfig&)`
- `start()` / `stop()`
- `grabFrame(Frame&)`
- `pollStats(CameraStats&)`
- `deliveryCapabilities()` / `activeDeliveryMode()` /
  `pollAcquisitionQueueStats(AcquisitionQueueStats&)`
- `checkDeviceHealth()`
- `configureTriggerOutput(lineSelector)` / `setTriggerOutput(high)`

## Frame delivery modes (issue #330)

Capabilities: `supportsEveryFrame=true`, `supportsLatestFrame=true`,
`modeChangeRequiresRestart=true`, `timestampsHostComparable=false`
(`frameHead.uiTimeStamp` is a device tick counter, not the host clock).

- **EveryFrame** (default): unchanged ordered `CameraGetImageBuffer`
  retrieval; frames are never intentionally skipped. The priority API is
  deliberately never used in this mode.
- **LatestFrame**: stale completed SDK buffers are dropped before the
  expensive `CameraImageProcess` copy so `grabFrame()` returns the freshest
  complete image. Two implementations, selected at compile time:
  - **SDK priority path** — when `CAMERA_GET_IMAGE_PRIORITY_NEWEST` is
    visible to the preprocessor (`#ifdef`), or the build passes
    `-DMIB_MINDVISION_USE_PRIORITY_API=1`, `grabFrame` calls
    `CameraGetImageBufferPriority(h, &head, &buf, 100,
    CAMERA_GET_IMAGE_PRIORITY_NEWEST)` and the SDK itself discards older
    completed buffers. **Limitation:** the SDK gives no per-skip callback, so
    `intentionallyDiscardedFrames` cannot count these SDK-internal skips
    exactly on this path.
  - **Bounded drain fallback** — otherwise: blocking
    `CameraGetImageBuffer(..., 100)` first, then at most
    `config_.numBuffers` non-blocking (`0` timeout) fetches; each newer
    completed buffer supersedes the held one, which is released immediately
    and counted. This path counts every intentional discard **exactly**.
  - Note: every public SDK mirror observed (2026-08) declares the priority
    constants as a plain C `enum emCameraGetImagePriority` in
    `CameraDefine.h`, which `#ifdef` cannot see — so on those SDKs the drain
    fallback is what actually compiles in unless the build opts in with
    `MIB_MINDVISION_USE_PRIORITY_API=1` after verifying the SDK ships
    `CameraGetImageBufferPriority`.

**Release-exactly-once invariant:** on every path through `grabFrame` —
priority, drain, EveryFrame, stop-while-holding, and `CameraImageProcess`
failure — each buffer acquired from the SDK is passed to
`CameraReleaseImageBuffer` exactly once. The drain loop holds exactly one
buffer at any moment (release old, then adopt new). Audit this whenever the
grab path changes.

Mode changes require a full `stop()` → `applyConfig()` → `start()` cycle;
the active SDK queue is never cleared or reordered mid-run
(no `CameraClearBuffer` on a running camera). `activeDeliveryMode()` reports
the mode confirmed at the most recent successful `start()`; before any start
it reports `config_.deliveryMode`.

## Acquisition-queue telemetry (issues #330/#333)

`pollAcquisitionQueueStats` returns `true` while running and fills:

- `deliveredFrames` — `frameCount_` (reset at `start()`).
- `intentionallyDiscardedFrames` — atomic counter (reset at `start()`;
  exact on the drain path, see limitation above for the priority path).
- `transportLostFrames` — `tSdkFrameStatistic::iLost` via
  `CameraGetFrameStatistic` (present in all known SDK variants, including the
  dynamic-loader `CameraApiLoad.h`), with `transportLossValid=true` when the
  call succeeds.
- **Explicitly unavailable** (left `0` with valid flags `false`, per #333):
  `sdkCompletedQueueDepth`, `sdkInputBufferCount`, `bufferUnderruns` — the
  MindVision SDK exposes no queue-depth or underrun observability.

## Platform behavior

- **Windows + `MIB_ENABLE_MINDVISION=ON`**: real SDK-backed implementation.
- **Windows + `MIB_ENABLE_MINDVISION=OFF`** and all non-Windows builds: safe
  stub that logs unsupported calls and returns failure/no-op.

## JSON config parsing (`MindVisionConfig.h`)

The config-file parse + validation is a pure, QtCore-only function shared by
both `MindVisionCamera::applyJsonConfig` and
[[../services/CameraControlService]]`::applyMindVisionJsonToCamera` (the two had
**drifted** — the camera applied ~19 fields, the control service only 7 — and
neither validated bounds):

- `backend::camera::mindvision::parseConfig(QByteArray)` → `ParseResult{ok,
  config, error, warnings}`. Malformed JSON or a non-object root → `ok=false`
  with `error`. Otherwise every numeric field is clamped to a safe range and one
  warning is recorded per clamp.
- Critical clamps: `width`/`height` forced `>= 1` (a non-positive ROI was
  unusable), `exposure_time_us > 0`, and **`strobe_pulse_width_us` /
  `strobe_delay_us` forced `>= 0`** — a negative value previously wrapped to a
  multi-second pulse when cast to the SDK's unsigned type.

Both call sites read the file, call `parseConfig`, log warnings, then apply
`config.*` to the SDK (the camera applies the full set; the control service
still applies only its historical subset).

## Gotchas

- The SDK is discovered at configure time through `MIB_MINDVISION_SDK_ROOT`
  or the `MIB_MINDVISION_SDK_DIR` environment variable.
- The implementation accepts both SDK include layouts:
  `MindVision/CameraApiLoad.h` and a flat `CameraApiLoad.h` directly under
  the configured include directory.
- `MindVisionCamera.cpp` owns the SDK dynamic-loader definitions by defining
  `API_LOAD_MAIN`; other MindVision users include the SDK header as extern
  declarations only.
- Runtime deployment copies the MindVision DLL next to the app when Windows
  packaging is enabled.
- The current backend treats MindVision as a separate camera provider; mock
  and EGrabber paths remain independent.
