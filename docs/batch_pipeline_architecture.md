# Batch Pipeline Architecture

## Goal

The live capture loop should publish frames without doing OpenCV segmentation
inline. Processing can run behind capture, in batches, with a bounded queue that
applies backpressure by dropping new enqueue attempts instead of blocking the
camera path.

## Model

1. **Ingest** - capture code calls
   `ProcessingService::enqueueBatchFrame(...)`. The call copies the frame into a
   bounded queue and returns `true` on acceptance or `false` when the queue is
   full. It does not run background subtraction, ROI masking, morphology, or
   contour analysis on the caller thread.
2. **Batch** - batch workers wait until `BatchPipelineConfig::batchSize` frames
   are queued. When the pipeline is stopped, workers drain any residual partial
   batch before exiting.
3. **Process** - each worker reuses
   `ProcessingService::computeProcessedFrame(...)`, the same pure helper used by
   offline remasking. This preserves the existing metric path for area,
   deformability, ring width (`ringRatio`), brightness quantiles, target-group
   gates, and optional Young's modulus lookup.
4. **Emit** - completed batches are delivered to
   `ProcessingService::BatchResultCallback` as `std::vector<ProcessedFrame>`.
   Emission runs on the batch worker, not the capture thread.

## Interfaces

`ProcessingService::BatchPipelineConfig` owns the async settings:

- `batchSize` - target frames per emitted batch.
- `maxQueuedFrames` - bounded queue capacity. When full,
  `enqueueBatchFrame()` returns `false`.
- `workerCount` - number of batch workers.
- `processing`, `background`, `roi` - the same legacy processing inputs used by
  `computeProcessedFrame()`.

`ProcessingService::BatchPipelineStats` exposes acceptance and processing
counters:

- `framesAccepted`, `framesDropped`, `framesProcessed`, `batchesProcessed`
- `currentQueueDepth`, `maxQueueDepth`
- effective `batchSize`, `workerCount`, and `running`

These counters are intended for capture-loop proof, status displays, and
review evidence.

## Capture-Loop Contract

The capture side only owns enqueue. The batch side owns segmentation and emit.
That means a capture path can call:

```cpp
processing.startBatchPipeline(config, onBatch);
processing.enqueueBatchFrame(grayFrame, writeIndex, timestampNs);
```

If the worker is slower than capture, accepted frames accumulate up to
`maxQueuedFrames`. Once the queue is full, new frames are rejected immediately.
The capture loop can count that as a drop and continue acquiring the next
frame.

## Migration Path

The legacy realtime path remains available while migration proceeds:

```text
FrameStore -> realtimeLoop -> background subtract -> ROI -> contours -> metrics
```

The async batch path introduces the same processing stages behind an ingest
queue:

```text
capture callback -> enqueueBatchFrame -> queue -> batch worker
  -> computeProcessedFrame(background subtract -> ROI -> contours -> metrics)
  -> BatchResultCallback
```

Initial consumers should use the async path for offline or non-trigger-critical
analysis where complete batches are acceptable. Trigger-critical realtime
behavior can continue using `startRealtime()` until downstream emit/storage code
is ready to consume `BatchResultCallback` output directly.

The app-level proof path uses the same production objects that mib studio starts
at runtime:

```text
AppBackend -> CaptureService -> MockCamera -> capture callback
  -> ProcessingService::enqueueBatchFrame -> batch workers -> BatchResultCallback
```

This keeps the queue contract executable through mib studio backend/runtime code
without requiring a hardware camera or blocking the Qt UI thread.

## Validation Hooks

`processing_batch_pipeline_test` verifies:

- enqueue succeeds while a worker callback is blocked,
- queue capacity drops without blocking,
- batch size and worker count are configurable,
- area, deformability, and ring-width metrics are present in batch results.

`kin6_batch_pipeline_evidence` plus
`tools/kin6_generate_hf_evidence.sh` verifies the Hugging Face
`gavinlouuu/512x96stream` 5,000-frame evidence run and writes reviewer-facing
visuals, metrics, and capture-loop timing under `review_artifacts/KIN-6/`.

`kin6_mib_app_capture_proof` boots `AppBackend`, configures `MockCamera`, starts
`CaptureService`, and enqueues every capture callback into the async batch
pipeline. With the Hugging Face bundle it verifies 5,000 capture callbacks,
5,000 accepted enqueues, zero drops, a configured 5,000-frame batch, and 5,000
processed outputs through mib studio software code.

`backend.kin10_hf_dataset_pipeline` is the CI/local integration harness for the
public Hugging Face dataset. It downloads stable sample rows from
`gavinlouuu/512x96stream` through the Dataset Viewer API without HF login, runs
them through `ProcessingService::startBatchPipeline()`, and fails with explicit
accepted/processed/drop, mask, contour, and metric-range regression messages.
See `docs/howto/hf-dataset-integration-tests.md` for local commands and cache
layout.
