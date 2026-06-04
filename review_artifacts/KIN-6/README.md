# KIN-6 Review Evidence

This bundle demonstrates the async batch pipeline added for KIN-6 using a synthetic ring frame.

Regenerate from the repository root:

```sh
cmake --build --preset linux-backend-only-build --target kin6_batch_pipeline_evidence
./build/linux-backend/kin6_batch_pipeline_evidence review_artifacts/KIN-6
```

Artifacts:

- `input_ring.png`: synthetic grayscale frame submitted through `enqueueBatchFrame()`.
- `processed_mask.png`: mask produced by the async worker via `computeProcessedFrame()`.
- `contour_overlay.png`: contour overlay from the emitted `ProcessedFrame` validation result.
- `metrics.json`: queue stats, callback batches, and emitted area/deformability/ring-width metrics.
- `logs/`: configure, build, test, evidence-generation, and PR-check logs captured during validation.
