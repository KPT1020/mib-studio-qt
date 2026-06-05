# Synthetic Condition Validation

Use `scripts/synthetic_condition_validation.py` to validate the existing
middle-band contour detection path against deterministic image-condition
variants from the Hugging Face dataset `gavinlouuu/512x96stream`.

The harness loads an exact split slice, keeps the source dataset unchanged,
generates baseline plus low/high and extreme low/high brightness and contrast
frames in memory, runs the processing path, and writes reviewer-facing images
and JSON.

## Regeneration Command

Run from the repository root:

```bash
HF_HOME=.cache/huggingface \
HF_DATASETS_CACHE=.cache/huggingface/datasets \
PYTHONPATH=.python_deps \
python scripts/synthetic_condition_validation.py \
  --dataset gavinlouuu/512x96stream \
  --dataset-config default \
  --split train \
  --sample-start 0 \
  --sample-count 8 \
  --output-dir review_artifacts/KIN-12 \
  --background-mode transformed \
  --background-sample-count 1 \
  --min-area 1
```

## Review Outputs

- `frames/`: baseline and transformed input frames.
- `masks/`: processed binary masks from detection.
- `overlays/`: input frames with band boundaries, mask tint, and contours.
- `metrics.json`: detection success/failure counts by condition plus parity
  against baseline.
- `report.html`: self-contained reviewer report with command table, flow
  diagram, visual gallery, metrics tables, limitations, and regeneration
  command.
- `manifest.json`: dataset split/sample range, transform definitions, key
  review paths, and per-artifact purpose/provenance/size metadata.
- `flow_diagram.svg`: synthetic validation data/control flow and evidence
  capture points.
- `sample_array_manifest.json`: deterministic reviewer sample cases covering
  the first empty baseline, first cell-positive baseline, and low-condition
  detection drops when the requested slice contains those cases.
- `sample_case_*.png`: contact sheets for the representative cases, with input,
  mask, and overlay columns for each selected condition.

Detection success is defined as at least one filtered contour returned by the
existing middle-band processing path. The default background mode builds a
condition-matched background from the first sampled frame for each transform.
The example uses `--min-area 1` because the stream slice contains small
components that the default `min-area=100` setting filters out before the
success/failure count stage.

The full bundle still writes every sampled frame for every condition. The
sample-array manifest highlights stable sample IDs and condition paths for
review, including cell detections from `gavinlouuu/512x96stream` when the
sampled slice contains baseline detections.
