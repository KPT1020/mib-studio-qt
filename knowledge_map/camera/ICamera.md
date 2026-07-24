# ICamera (interface)

> Abstract base for any frame source. Deliberately small; normalizes
> hardware vs mock differences so [[../services/CaptureService]] doesn't
> care.

**Source:** `include/backend/camera/common/ICamera.h`,
`include/backend/camera/common/Frame.h`,
`include/backend/camera/common/WindowedRate.h` (host-side rate estimator for
SDKs without device-side stats)

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
- `configureTriggerOutput`'s `lineSelector` is vendor-interpreted: EGrabber
  treats it as a GenICam line name (e.g. `"TTLIO12"`); [[MindVisionCamera]]
  ignores it and uses `trigger_output_index` from its JSON config.
- `Frame.timestamp` is a per-vendor opaque device clock; use
  `hostTimestampUs` (stamped by [[../services/CaptureService]]) for
  cross-vendor latency math.
