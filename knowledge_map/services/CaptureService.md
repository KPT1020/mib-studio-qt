# CaptureService

> Owns the acquisition thread. Calls `camera->grabFrame()` in a loop and
> pushes each frame into [[../data-model/FrameStore]].

**Source:** `src/backend/services/CaptureService.cpp`,
`include/backend/services/CaptureService.h`
**Related:** [[../camera/ICamera]], [[../data-model/FrameStore]],
[[ProcessingService]], [[TriggerService]]

## Responsibility

- One thread per service: `run()` blocks on the camera's blocking `grabFrame`.
- Stamps `Frame::hostTimestampUs` (host monotonic µs, `Tools::getTimestamp`)
  the moment `grabFrame` returns — the acquisition anchor for all
  downstream latency measurement ([[../diagnostics/PipelineTimingRecorder]]).
  The camera's own `timestamp` is a device tick on a different clock.
- Copies frame bytes into `FrameStore` (including the host stamp) and fires
  `FrameCallback` (used by UI for live preview).
- Exposes `CaptureStats` — `framesProcessed`, `lastFrameRate`,
  `lastDataRateMBps` (the latter two come from EGrabber StreamModule), plus
  delivery-mode and acquisition-queue telemetry: requested vs
  backend-confirmed mode (`deliveryModeConfirmed`), intentional discards,
  transport loss, underruns, SDK queue depths, `lastFrameAgeUs` (only when
  the backend reports host-comparable timestamps), and
  `lastPublishLatencyUs` (dequeue → post-publish copy duration).
- Owns the delivery-mode handshake: pre-checks the requested
  `FrameDeliveryMode` against `deliveryCapabilities()` (unsupported mode →
  actionable `runtime_error`, capture never starts), forwards the mode via
  `CameraConfig`, records the backend-confirmed mode after `start()`, and
  warns (rate-limited to the 1 s stats poll) when an EveryFrame backlog is
  growing. `activeDeliveryMode()` is what the UI badge should display.

## Key APIs

```cpp
void setConfig(const Config& cfg);              // bufferPartCount, numBuffers, deliveryMode
camera::common::FrameDeliveryMode activeDeliveryMode(); // backend-confirmed
void setFrameCallback(FrameCallback cb);        // UI live preview hook
void setFrameStore(shared_ptr<FrameStore>);     // ring buffer sink
void setCameraFactory(CameraFactory);           // injects ICamera builder
void setCameraReadyCallback(CameraReadyCallback); // fires with ptr on start, nullptr on stop
bool start();  void stop();  bool isRunning();
```

Camera factory is how `AppBackend` chooses between
[[../camera/EGrabberCamera]] and [[../camera/MockCamera]] without this
service knowing which one.

`setConfig` has its single call site in
`frontend::AppConfigWatcher::loadAndApplyFromPath` (fed by the
`camera.frame_delivery_mode` profile key and the
[[../frontend/ConnectTab]] delivery-mode combo).
`deliveryModeConfirmed` is cleared when the capture loop releases the
camera, so `activeDeliveryMode()` falls back to the requested config mode
between runs.

## Threading

Dedicated thread. `grabFrame` blocks until a frame is available or `stop()`
returns false. On stop, this thread is joined before [[ProcessingService]]'s
realtime loop shuts down.

## Gotchas

- Always refresh EGrabber `StreamModule` stat counters before calling
  `stop()` — see [[../conventions/Code-Conventions]] and
  `docs/howto/safe-start-stop-egrabber.md`.
- `setCameraReadyCallback` fires from this thread; [[TriggerService]] uses
  it to grab a live `ICamera*` and start itself.
- `stop()` (GUI thread) invokes `cameraReadyCallback_(nullptr)` **before**
  `activeCamera_->stop()` so the trigger thread is stopped before the grabber
  is torn down; `releaseCamera()` on the capture thread repeats the call
  (idempotent). Without this ordering a pending trigger raced the grabber
  teardown (use-after-free inside the SDK).
- Platform default factory:
  - Windows (`MIB_HAS_EGRABBER=1`) defaults to [[../camera/EGrabberCamera]].
  - Non-Windows defaults to [[../camera/MockCamera]] (`data/mock_frames`) so
    cloud/Linux builds can exercise non-hardware pipeline paths.
