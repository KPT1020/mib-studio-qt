# Batch Pipeline

> Async frame queue architecture for decoupling capture rate from image
> processing throughput.

**Source:** `include/backend/services/ProcessingService.h`,
`src/backend/services/ProcessingService.cpp`
**Related:** [[Data-Flow]], [[Threading-Model]],
[[../services/CaptureService]], [[../services/ProcessingService]],
[[../domain/Microscopy-Pipeline]]

## Goal

The legacy realtime path polls `FrameStore` and performs segmentation,
contour extraction, validation, and metric emission on one realtime processing
thread. That keeps latency low, but it couples the effective capture backlog to
per-frame processing cost.

The async batch path introduces an opt-in queue inside
`ProcessingService`:

```
CaptureService FrameCallback
  -> ProcessingService::enqueueBatchFrame(...)
  -> bounded queue
  -> N batch workers
  -> computeProcessedFrame(...)
  -> BatchResultCallback(vector<ProcessedFrame>)
```

`enqueueBatchFrame()` copies the raw camera payload into a `cv::Mat`, checks a
bounded queue, and returns immediately. If the queue is full, it returns
`false` and increments `framesDropped`; it does not wait for processing
workers. This gives the capture thread a non-blocking backpressure contract.

## Interfaces

- **Ingest:** `ProcessingService::enqueueBatchFrame(...)` accepts the same
  metadata shape as `CaptureService::FrameCallback` and
  `backend::playback::Frame`: width, height, line pitch, pixel format,
  timestamp, and an optional frame index.
- **Batching:** `BatchPipelineConfig::batchSize` controls how many queued
  frames a worker drains per unit of work. Workers process whatever is
  available, so sparse streams do not wait indefinitely for a full batch.
- **Concurrency:** `BatchPipelineConfig::workerCount` controls the number of
  batch workers started by `startBatchPipeline()`.
- **Backpressure:** `BatchPipelineConfig::maxQueuedFrames` bounds memory. Full
  queue means the newest enqueue attempt fails fast and `framesDropped`
  increments.
- **Processing:** Workers call `computeProcessedFrame()` for each frame, using
  `BatchPipelineConfig::processingConfig`, `roi`, and `backgroundGray`.
- **Emit:** `BatchResultCallback` receives a moved
  `std::vector<ProcessedFrame>`. The callback must stay short or hand off work
  to another service; a slow callback only stalls batch workers, not capture
  enqueue.
- **Stats:** `getBatchPipelineStats()` reports enqueued, dropped, processed,
  batches processed, queued frames, and running state.

## Metric Parity

The async path reuses `computeProcessedFrame()`, the same pure helper used by
offline `processBatch()`. That means these `FilterResult` fields remain
available from `ProcessedFrame::validation`:

- `area`
- `deformability`
- `ringRatio` (ring width/focus metric)
- `areaRatio`
- `youngsModulus`
- contour hierarchy and brightness quantiles

## Migration Path

1. Keep `CaptureService -> FrameStore -> realtimeLoop()` as the default live
   path while the batch path is validated.
2. Wire `CaptureService::setFrameCallback()` to call
   `ProcessingService::enqueueBatchFrame()` for opt-in experiments or backend
   tests. Continue writing to `FrameStore` for preview/playback.
3. Route `BatchResultCallback` to the same consumers currently fed by
   realtime processing:
   - monitoring buffers/UI summaries,
   - experiment accumulation/HDF5 flush,
   - target-group trigger result,
   - ring-ratio autofocus samples.
4. Move the legacy per-frame steps in this order:
   background subtract -> ROI clamp -> threshold/morphology -> contours ->
   validation/metrics. `computeProcessedFrame()` already covers this pure
   subset; auto-background capture and multi-image series state remain realtime
   concerns until they get explicit batch-safe state machines.
5. After callback consumers are parity-tested, retire the realtime FIFO polling
   path or keep it as a low-latency preview mode while batch workers own
   experiment persistence.

## Current Boundaries

- The batch API is backend-only and opt-in; no GUI control enables it yet.
- The queue stores copied grayscale frames, so capture is not tied to camera
  buffer lifetime.
- Worker concurrency can reorder emitted batches. Consumers that require
  monotonic frame order should sort or re-sequence by `ProcessedFrame::index`.
- Auto-background and multi-image series behavior are intentionally not moved
  in this ticket; they need separate state handling before full replacement of
  `realtimeLoop()`.
