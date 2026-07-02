# CaptureService

> Owns the acquisition thread. Calls `camera->grabFrame()` in a loop and
> pushes each frame into [[../data-model/FrameStore]].

**Source:** `src/backend/services/CaptureService.cpp`,
`include/backend/services/CaptureService.h`
**Related:** [[../camera/ICamera]], [[../data-model/FrameStore]],
[[ProcessingService]], [[TriggerService]]

## Responsibility

- One thread per service: `run()` blocks on the camera's blocking `grabFrame`.
- Copies frame bytes into `FrameStore` and fires `FrameCallback` (used by UI
  for live preview).
- Exposes `CaptureStats` — `framesProcessed`, `lastFrameRate`,
  `lastDataRateMBps` (the latter two come from EGrabber StreamModule).

## Key APIs

```cpp
void setConfig(const Config& cfg);              // bufferPartCount, numBuffers
void setFrameCallback(FrameCallback cb);        // UI live preview hook
void setFrameStore(shared_ptr<FrameStore>);     // ring buffer sink
void setCameraFactory(CameraFactory);           // injects ICamera builder
void setCameraReadyCallback(CameraReadyCallback); // fires with ptr on start, nullptr on stop
bool start();  void stop();  bool isRunning();
```

Camera factory is how `AppBackend` chooses between
[[../camera/EGrabberCamera]] and [[../camera/MockCamera]] without this
service knowing which one.

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
  is torn down; `releaseCamera()` on the capture thread repeats the call.
  Without this ordering a pending trigger raced the grabber teardown
  (use-after-free inside the SDK). This means the callback (and therefore
  [[TriggerService]]::stop()) can genuinely race between the GUI thread and
  the capture thread — see [[TriggerService]]'s "Concurrent-stop() fix"
  gotcha for why `TriggerService::stop()` had to be made safe against being
  entered by two threads at once, not just safe to call twice sequentially.
- Platform default factory:
  - Windows (`MIB_HAS_EGRABBER=1`) defaults to [[../camera/EGrabberCamera]].
  - Non-Windows defaults to [[../camera/MockCamera]] (`data/mock_frames`) so
    cloud/Linux builds can exercise non-hardware pipeline paths.
