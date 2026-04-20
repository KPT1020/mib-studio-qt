# YoloService

> Owns an optional ONNX Runtime segmentation session. By default it loads
> `resources/models/yolo11n-seg.onnx` during `AppBackend::initialize`, and can
> now execute lightweight U-Net style segmentation inference.

**Source:** `src/backend/services/YoloService.cpp`,
`src/backend/services/YoloService.stub.cpp`,
`include/backend/services/YoloService.h`

## Responsibility

- `initialize(modelPath)` — load the model; returns false on failure (app
  continues without YOLO features).
- `isLoaded()` — whether the model is usable.
- `inferSegmentationMask(grayInput, outMask, threshold)` — run ONNX inference
  and produce a binary `CV_8UC1` mask (0/255). Handles common segmentation
  output conventions:
  - binary logits/probabilities (`1x1xHxW` or `1xHxW`)
  - multi-class logits (`1xCxHxW`) via argmax (class 0 background)
  - NHWC output variants (`1xHxWxC`)
- `getSession()` / `getEnv()` — hand out the raw `Ort::Session*` /
  `Ort::Env*` for callers that want to run inference.

## Threading

`inferSegmentationMask` is internally serialised with a mutex around session
execution, so callers (e.g. [[ProcessingService]]) can safely invoke it from the
realtime loop without adding external ONNX session locking.

## Gotchas

- `ProcessingService` now has an optional inference path controlled by config
  (`image_processing.lightweight_unet.enabled`). When disabled or inference
  fails, processing falls back to the existing OpenCV threshold+morphology
  path.
- Model path resolution is relative to the executable directory (see
  `AppBackend::initialize` wiring).
- If the model is missing, `AppBackend` logs a warning via spdlog and
  continues.
- CMake now treats ONNX Runtime as optional (`find_package(onnxruntime CONFIG QUIET)`):
  - with `onnxruntime::onnxruntime`, real implementation (`YoloService.cpp`) is built
  - without it, a stub (`YoloService.stub.cpp`) is built and `initialize()`
    returns `false` after one warning log
