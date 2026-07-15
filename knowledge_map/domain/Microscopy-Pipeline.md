# Microscopy Pipeline

> What the app is actually measuring and why. Read alongside
> [[../services/ProcessingService]] and [[../architecture/Data-Flow]].

## What the app does

Microfluidic deformability cytometry: cells flow through a microfluidic
channel, a high-speed camera captures their passage, and software extracts
per-cell mechanical properties (deformability, area, stiffness).

## Per-frame work

1. **Acquire** — camera pushes an 8-bit grayscale image into
   [[../data-model/FrameStore]]. Frame rate is typically hundreds to
   thousands of FPS (EGrabber + fast CoaXPress camera).
2. **Background subtract** — optional; enabled via
   `ProcessingService::setRealtimeBackgroundGray`. Can be auto-captured
   after N consecutive empty frames.
3. **Segment** — Gaussian blur → fixed-threshold → morphological open/close
   → `cv::findContours`.
4. **Classify** — each contour is scored by the `ProcessingConfig` gates:
   - Border-touching? → invalid
   - Single inner contour required?
   - Area in `[area_threshold_min, area_threshold_max]` (μm²)
   - Deformability in range
   - Ring ratio in range (for focus check)
5. **Metrics** — `FilterResult` captures:
   `area`, `deformability`, `areaRatio`, `ringRatio`, `youngsModulus` (LUT),
   `brightness` quantiles.
6. **Target-group gate** — valid frames pass a second gate; matching
   frames fire a camera trigger pulse via [[../services/TriggerService]].

## Multi-image series mode

When `ProcessingConfig::multi_image_enabled` is true, each valid detection
captures `multi_image_count` consecutive frames (the "trigger" frame plus
subsequent ones). Metrics are computed only from the first frame.
`ProcessedFrame::seriesImages` carries the series; stored as 4D
`(N, seriesCount, H, W)` in HDF5.

## Focus feedback

`ring_ratio` is fed to [[../services/AutofocusService]] which drives a
piezo nanopositioner to keep the cell in focus across the channel.

## See also

- [[Glossary]] for term definitions.
- `docs/gold_standard_metrics.md` — JSON format for comparing this
  pipeline's outputs against the legacy pipeline.
- `scripts/empty_frame_detection.py` — offline Python pipeline (Kedro +
  MLflow at `mlflow.yofo.bio`). Not part of the Qt app's runtime.
