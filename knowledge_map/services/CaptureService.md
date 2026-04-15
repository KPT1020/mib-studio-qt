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
- Computes a **predicted CPU capture timestamp** per frame
  (`frame.timestamp` + EMA-smoothed hw→CPU offset) and passes it to
  `FrameStore::pushFrame(..., captureSteadyNs)`. Downstream consumers
  (notably [[TriggerService]]) use this as the scheduling anchor, so
  trigger onsets land on the camera's periodic hardware grid regardless
  of CPU-side jitter caused by processing-pipeline load.
- Exposes `CaptureStats` — `framesProcessed`, `lastFrameRate`,
  `lastDataRateMBps`, plus `clockOffsetNs` (smoothed hw→CPU offset) and
  `lastRawOffsetNs` (latest raw offset sample) for diagnostics.

## Clock-offset tracking

Raw `steady_clock::now()` at `grabFrame()` return is jittered by CPU
scheduling (which is driven by processing-pipeline load). The hardware
timestamps from EGrabber (`BUFFER_INFO_TIMESTAMP` / per-part timestamps,
nanoseconds) arrive on a periodic jitter-free grid. We maintain a slow
EMA of `cpu_observed_ns - hw_ns` (weight `1/64`) and publish the frame's
predicted CPU-clock capture time as `hw_ns + smoothed_offset`.

On the first frame (or after a sudden hw-clock jump > 100 ms, e.g. camera
re-arm), the smoothed offset is re-bootstrapped from the raw sample and a
WARN is logged. For cameras with no hardware timestamp (`frame.timestamp
== 0`, e.g. some mock configs), we fall back to raw `steady_clock::now()`.

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
