# 5000 fps camera setup and test (using @cpp sample setup.js)

- Goal: Achieve and verify continuous ≥5000 fps capture with eGrabber, CPU-first processing.
- References (samples):
  - High frame rate loops: `311-high-frame-rate.cpp`, `310-high-frame-rate.cpp`
  - Config scripts: `config-rg.js` (RG mode), `config-sc.js` (SC mode), `101-singleframe.setup.js`, `213-egrabbers.setup.js`

## Key parameters for 5000 fps
- Operating mode: `RG` (rolling/global as appropriate) or `SC`.
- Cycle period: set `CycleMinimumPeriod` = 1e6 / fps (µs); for 5000 fps → 200 µs.
- Exposure: must be ≤ period; start with 80–120 µs (tune per sensor). Use `ExposureReadoutOverlap=true` when supported.
- Pixel format: `Mono8` to minimize bandwidth.
- Optional: reduce ROI (Width/Height) if data rate is too high for link.
- Stream: `BufferPartCount=100`, `reallocBuffers(20)` as in high-fps samples.

## Example setup.js (based on config-rg.js) for 5000 fps
```javascript
var configure = require('egrabber://configurator.js');

function fpsToMicroseconds(fps) { return 1e6 / fps; }

var parameters = {
    OperatingMode:              "RG",
    ExposureReadoutOverlap:     true,
    ExposureRecoveryTime:       100,           // tune per camera
    CycleMinimumPeriod:         fpsToMicroseconds(5000), // 200 µs
    ExposureTime:               100,           // µs; must be < 200
    StrobeDuration:             50,
    StrobeDelay:                0,
    CycleTriggerSource:         "Immediate"
};

configure(grabbers[0], parameters);

// Ensure compact format to reduce bandwidth (if camera supports)
if (grabbers[0].StreamPort.get("PixelFormat") !== "Mono8") {
    grabbers[0].RemotePort.set("PixelFormat", "Mono8");
}
```

## Where these patterns come from
- High-rate buffer processing pattern (process parts as delivered; round-robin buffers): see `311-high-frame-rate.cpp`.
- Period/Exposure pattern and configurator usage: see `config-rg.js` and `config-sc.js`.

## Test procedure
1. Apply the setup.js above (or adapt `config-rg.js`) via the EGrabber sample harness to configure the camera at 5000 fps.
2. Use our console test `capture_processing_test` to run capture for a fixed duration (e.g., 2 s); confirm frames are processed and no blocking occurs.
3. Monitor stream stats (`StatisticsFrameRate`, `StatisticsDataRate`) and logs.
4. If link/camera cannot sustain data rate, reduce ROI or switch to `Mono8` and retune `ExposureTime`.

## Backend alignment
- `CaptureService`: set `BufferPartCount=100`, `reallocBuffers(20)`. Stats polled each second.
- `ProcessingService`: CPU worker queue to avoid blocking capture thread.

## Notes
- Actual achievable fps depends on sensor timing limits and link bandwidth; 5000 fps requires very small ROI or very short exposure.
- For line-scan cameras, use line-rate controls rather than frame-rate; adapt parameters accordingly.
