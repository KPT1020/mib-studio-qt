# ICamera (interface)

> Abstract base for any frame source. Deliberately small; normalizes
> hardware vs mock differences so [[../services/CaptureService]] doesn't
> care.

**Source:** `include/camera/common/ICamera.h`, `include/camera/common/Frame.h`

## Types

- `camera::common::Frame` — `{ width, height, pixelFormat (PFNC),
  linePitch, timestamp, data }`.
- `CameraConfig` — `{ bufferPartCount = 1, numBuffers = 20 }`.
- `CameraStats` — `{ frameRate, dataRateMBps }`.

## Virtual methods

```cpp
virtual void applyConfig(const CameraConfig& config) = 0;
virtual bool start() = 0;
virtual void stop()  = 0;
virtual bool isRunning() const = 0;

virtual bool grabFrame(Frame& out) = 0;          // blocks; false on stop
virtual bool pollStats(CameraStats& out) const = 0;

virtual bool checkDeviceHealth() const { return true; }
virtual void configureTriggerOutput(const std::string& lineSelector) {}
virtual bool setTriggerOutput(bool high) { return false; }
```

## Related

- [[EGrabberCamera]] — hardware implementation
- [[MockCamera]] — folder-backed implementation
- [[../services/TriggerService]] calls `setTriggerOutput` via the live
  camera pointer
- [[../services/CameraControlService]] uses the Euresys SDK directly for
  discovery; it does not go through `ICamera`.

## Gotchas

- `pixelFormat` uses Euresys PFNC codes (see [[../domain/Glossary]]).
- `linePitch` may exceed `width` (stride padding) — always honor it when
  iterating frame data.
