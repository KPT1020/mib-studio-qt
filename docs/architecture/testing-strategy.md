# Testing Strategy

Backend tests are organized by subsystem and registered through
`tests/CMakeLists.txt`. The Linux backend-only preset is the primary CI path
because it exercises real backend code without proprietary camera SDKs.

## Test Layout

```text
tests/
  backend/
    backend_lifecycle_test.cpp
  camera/
    mock_camera_test.cpp
  processing/
    processing_batch_pipeline_test.cpp
    processing_multi_object_test.cpp
    processing_object_tracking_test.cpp
  recording/
    hdf5_smoke_test.cpp
  integration/
    kin6_mib_app_capture_proof.cpp
    kin10_hf_dataset_pipeline_test.cpp
  python/
    test_synthetic_condition_validation.py
```

## Test Categories

Unit tests cover deterministic service behavior:

- Mock camera starts, delivers a frame from fixture data, reports stats, and
  stops without hardware.
- Processing handles synthetic frames and object tracking cases.
- HDF5 recording services create and reopen expected datasets.

Integration tests connect multiple backend modules:

- `backend.lifecycle` initializes `backend::AppBackend` in mock-camera mode.
- Existing KIN integration tests connect capture, processing, and evidence
  generation paths.

Smoke tests verify major systems can start and stop with the backend-only
configuration.

## Required Local Validation

```text
cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build
ctest --preset linux-backend-only-test --output-on-failure
```

Each command should be captured with start/end timestamps and exit code in the
review bundle. CI runs the same backend-only path on Linux.

## Mock Camera Requirements

Mock camera tests use fixture images under `data/mock_frames` and must not
require physical hardware or proprietary SDKs. New processing tests should use
synthetic frames or stable fixture data so failures are reproducible.
