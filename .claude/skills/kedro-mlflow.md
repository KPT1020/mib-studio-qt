# kedro-mlflow

Run the Kedro empty-frame-detection pipeline and upload results to MLflow.

## Instructions

Run the full Kedro `empty_detection` pipeline from `scripts/kedro_frame_detection/`.
The pipeline loads the dataset, builds a background, processes frames, evaluates metrics,
saves artifacts locally, and logs everything (params, metrics, annotated + intermediate images) to MLflow.

### Steps

1. **Ensure dependencies are installed** — run `pip install -e .` inside `scripts/kedro_frame_detection/` if needed.

2. **Run the pipeline** from the Kedro project directory:

```bash
cd scripts/kedro_frame_detection && kedro run
```

The pipeline uses the MLflow Python client which reads credentials from the S3-backed
MLflow server at `https://mlflow.yofo.bio` (configured in `conf/base/parameters.yml`).
No separate S3 or MLflow credentials are needed — the server proxies artifact storage.

3. **If `kedro run` fails**, check that:
   - The virtual environment has all dependencies (`pip install -e .`)
   - The MLflow server is reachable (`curl -s https://mlflow.yofo.bio/health`)
   - If the MLflow client upload fails, the pipeline automatically falls back to the REST API

4. **Report the MLflow run URL** from the pipeline output back to the user.

### Pipeline parameters

Parameters live in `scripts/kedro_frame_detection/conf/base/parameters.yml`:
- `dataset_name` — HuggingFace dataset identifier
- `processing` — image processing config (blur, threshold, morphology, band fraction)
- `output_dir` — local artifact staging directory
- `mlflow.tracking_uri` — MLflow server URL
- `mlflow.experiment_name` — experiment name in MLflow
- `mlflow.run_name` — run name within the experiment

Users can override any parameter with `kedro run --params key:value`.
