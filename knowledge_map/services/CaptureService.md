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
