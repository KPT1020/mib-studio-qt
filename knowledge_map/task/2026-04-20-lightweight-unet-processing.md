# 2026-04-20 — Lightweight U-Net path in processing pipeline

## Summary

- Added an optional lightweight U-Net segmentation path that runs through
  `YoloService` ONNX Runtime inference and feeds binary masks into
  `ProcessingService` before contour validation.
- Kept the existing classical OpenCV path as strict fallback when ONNX is
  unavailable, model inference fails, or U-Net mode is disabled.

## Code changes

- `include/backend/services/YoloService.h`
  - Added `inferSegmentationMask(const cv::Mat&, cv::Mat&, float)` API.
  - Added cached model metadata (`inputName_`, `outputName_`, shapes) and
    `inferenceMutex_`.
- `src/backend/services/YoloService.cpp`
  - During `initialize()`, capture input/output names and tensor shapes.
  - Implement inference for common segmentation output layouts:
    - NCHW `[1,C,H,W]`
    - NHWC `[1,H,W,C]`
    - `[1,H,W]`
  - Support binary (`C=1`, sigmoid/threshold) and multi-class (`argmax`,
    foreground = class > 0) outputs.
  - Resize output masks back to request size with nearest-neighbor.
- `src/backend/services/YoloService.stub.cpp`
  - Added no-op `inferSegmentationMask()` implementation returning `false`.
- `include/backend/services/ProcessingService.h`
  - Added config fields:
    - `use_lightweight_unet`
    - `lightweight_unet_threshold`
  - Added `SegmentationMaskCallback` and
    `setSegmentationMaskCallback(...)`.
- `src/backend/services/ProcessingService.cpp`
  - Added callback setter implementation.
  - In `computeProcessedFrame()` and all realtime branches, attempt U-Net mask
    generation first when enabled; fallback to classical threshold+morphology.
  - Kept downstream validation/filtering and callback ordering unchanged.
- `src/backend/AppBackend.cpp`
  - Wired `ProcessingService::setSegmentationMaskCallback` to
    `YoloService::inferSegmentationMask`.
- `src/frontend/system/AppConfigWatcher.cpp`
  - Added JSON parsing for:
    - `image_processing.lightweight_unet.enabled`
    - `image_processing.lightweight_unet.threshold` (clamped [0,1])
  - Added runtime log line when U-Net mode is enabled.
- `resources/defaults/config.json`
  - Added default lightweight U-Net config block under `image_processing`.

## Notes

- This change uses existing optional ONNX wiring (`MIB_HAS_ONNXRUNTIME` +
  `YoloService.stub.cpp`) so Linux/cloud builds without ONNX Runtime continue
  to compile and run with the classical path.
