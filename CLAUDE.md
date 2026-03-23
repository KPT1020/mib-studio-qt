# CLAUDE.md

## Project Overview

MIB Studio Qt is a C++ (Qt/CMake) application for real-time microfluidic image-based (MIB) cell deformability analysis. It captures camera frames, runs an image processing pipeline, and stores results in HDF5 files. Python scripts in `scripts/` handle post-experiment export and reanalysis.

## Build

```bash
conan install . -of build --build=missing -s build_type=Release
cmake --preset windows-default
cmake --build build --config Release
```

## Algorithm Experiment Workflow — MLflow Upload Requirement

When running algorithm experiments (reanalysis, parameter sweeps, pipeline comparisons), **all intermediate images and results must be uploaded to MLflow** at `mlflow.yofo.bio`.

### What to upload

- **Intermediate pipeline images**: original, blurred, diff (background-subtracted), thresh (binary threshold), mask (morphology result), overlay (contour rendering)
- **Metrics CSV**: per-frame deformability, area, area_ratio, ring_ratio, brightness quantiles, validation flags
- **Processing parameters**: blur size, threshold, morph kernel, area thresholds, ROI — logged as MLflow params
- **Summary metrics**: valid/invalid frame counts, mean deformability, mean area — logged as MLflow metrics
- **Experiment metadata**: source HDF5 path, experiment timestamps, config.json snapshot

### MLflow server

- **Tracking URI**: `https://mlflow.yofo.bio`
- Log artifacts via `mlflow.log_artifact()` or `mlflow.log_artifacts()`
- Log parameters via `mlflow.log_param()` / `mlflow.log_params()`
- Log metrics via `mlflow.log_metric()` / `mlflow.log_metrics()`

### Example integration pattern

```python
import mlflow

mlflow.set_tracking_uri("https://mlflow.yofo.bio")
mlflow.set_experiment("mib-reanalysis")

with mlflow.start_run(run_name="experiment_xyz"):
    # Log processing parameters
    mlflow.log_params({"blur": 3, "threshold": 8, "morph_kernel": 3})

    # Run pipeline, save intermediates to output_dir ...

    # Log all intermediate images and results
    mlflow.log_artifacts(str(output_dir))

    # Log summary metrics
    mlflow.log_metrics({"valid_frames": 1234, "mean_deformability": 0.42})
```

## Key Paths

| Path | Purpose |
|------|---------|
| `src/backend/services/ProcessingService.cpp` | Core image processing pipeline |
| `src/frontend/controllers/ExperimentController.cpp` | Experiment lifecycle |
| `src/backend/services/Hdf5Service.cpp` | HDF5 data persistence |
| `scripts/reanalyse_hdf5.py` | Post-experiment reanalysis with intermediate images |
| `scripts/export_hdf5.py` | HDF5 → CSV/TIFF export |
| `resources/defaults/config.json` | Default processing parameters |

## Testing

Python scripts can be run directly:
```bash
python scripts/reanalyse_hdf5.py -i experiment.h5 -o ./reanalysis
python scripts/export_hdf5.py -i experiment.h5 -o ./export
```
