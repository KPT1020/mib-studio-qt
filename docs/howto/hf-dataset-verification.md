# HuggingFace Dataset Verification & CNN Training Data Collection

This how-to covers running an image dataset from the HuggingFace hub
(e.g. `gavinlouuu/512x96stream`) through the production C++ processing
pipeline. Two goals, one workflow:

1. **Verify the algorithm**: the frames are processed by the same
   `ProcessingService` the live app uses, so any per-frame metric or
   valid/invalid decision matches what would happen on a live camera.
2. **Collect CNN training data**: the resulting HDF5 is exported to a flat
   `images/ + masks/ + labels.csv` layout ready for PyTorch/TensorFlow
   data loaders.

## Pipeline overview

```
HF dataset --[scripts/hf_dataset_download.py]--> PNG folder
  \                                                 |
   \----[MIB_CAMERA_MODE=mock, MIB_MOCK_CAMERA_DIR=<folder>]---.
                                                               |
                                                               v
                        build/Release/hf_pipeline_runner       |
                               |                               |
                               v                               |
                        experiment.h5 <---ProcessingService----`
                               |
                               v
        scripts/hf_cnn_export.py  -->  cnn_dataset/
                                        ├─ images/000000.png, ...
                                        ├─ masks/000000.png, ...
                                        ├─ labels.csv
                                        ├─ dataset_info.json
                                        └─ background.png (optional)
```

## 1. Download the HF dataset

Install deps once:

```bash
pip install -r scripts/requirements.txt
```

Fetch a dataset into a flat folder of grayscale PNGs:

```bash
python scripts/hf_dataset_download.py \
    --dataset gavinlouuu/512x96stream \
    --split train \
    --out data/hf_frames \
    --limit 500       # optional; omit for the full split
```

Frames are written as `frame_000000.png`, `frame_000001.png`, ... in HF row
order. A `hf_manifest.json` sidecar records the dataset id, split, and
image column for later provenance.

If the dataset is gated, pass `--token $HF_TOKEN` (or export `HF_TOKEN` via
`huggingface-cli login`).

## 2. Run the C++ pipeline over the folder

Build the runner (target is in the default test-enabled config):

```powershell
cmake --build build --config Release --target hf_pipeline_runner
```

Run it:

```powershell
build\Release\hf_pipeline_runner.exe `
    --input data\hf_frames `
    --output data\hf_verify.h5 `
    --config resources\defaults\config.json
```

Useful flags:

| Flag | Default | Purpose |
|---|---|---|
| `--interval-ms <n>` | `1` | MockCamera frame interval. `0` is fastest. |
| `--invalid-sample-rate <n>` | `1` | Save every Nth invalid frame. `1` keeps all — the right default for CNN training. |
| `--flush-every <n>` | `200` | Periodic HDF5 flush cadence to bound RAM. |
| `--background <path>` | — | Pre-load a background image into `ProcessingService` (same semantics as the UI "Set Background" button). |
| `--data-dir <dir>` | `data` | Working dir for logs + SQLite scratch. |

On completion the runner prints a summary and exits 0. The resulting HDF5 has
the standard experiment schema: `/valid_frames/{images,masks,metadata}`,
`/invalid_frames/{images,masks,metadata}`, and `/experiment_info` with the
raw `config.json` on the `config_json` attribute.

Open `data\hf_verify.h5` in the **HDF Review** tab of `mib_studio_qt.exe`
for interactive inspection (scatter plot + per-frame thumbnails).

## 3. Export a CNN-training corpus

Convert the HDF5 into a flat, ML-friendly layout:

```bash
python scripts/hf_cnn_export.py \
    --input data/hf_verify.h5 \
    --output data/cnn_dataset
```

`labels.csv` has one row per frame with:

- `index, source_dataset, source_split, source_row` (provenance)
- `image_path, mask_path` (relative to the output dir; `mask_path` is empty for
  rejected frames unless `--write-invalid-masks` is passed)
- `is_valid, is_target_group, touches_border, has_single_inner_contour, inner_contour_count`
- `rejection_reason` — derived from stored metadata with the same precedence as
  `ProcessingService::filterProcessedImage`. One of:
  `valid`, `no_contours`, `no_single_inner_contour`, `touches_border`,
  `area_out_of_range`, `ring_ratio_out_of_range`, `deformability_out_of_range`
- `deformability, area_px, area_microns, area_ratio, ring_ratio`
- `brightness_q1..q4, youngs_modulus`

`dataset_info.json` pins the processing config, frame counts, ROI, and HF
provenance so the corpus is fully self-describing.

## Spot-checking the algorithm

1. Pick 5 rows from `labels.csv` with `is_valid == 1`.
2. Open the matching `images/<i>.png` and `masks/<i>.png` side-by-side.
3. Confirm the mask cleanly segments the cell and that
   `deformability`, `area_microns`, `ring_ratio` match expectations.

For a pixel-level reanalysis (e.g. to sweep thresholds without re-running
the C++ pipeline), use `scripts/reanalyse_hdf5.py` against the same
`hf_verify.h5`.

## Troubleshooting

- **`MockCamera: no images found in <dir>`** — the folder is empty or uses
  unsupported extensions (`.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, `.tiff`
  only). Re-run `hf_dataset_download.py`.
- **Runner exits with 0 frames processed** — usually an empty folder or a
  camera-factory failure; check the log under `<data-dir>/logs/app.log`.
- **All frames come back invalid** — verify the HF dataset dimensions
  (e.g. 512×96) match what the processing config expects, and consider
  passing a `--background` image. Background-less detection requires the
  foreground to exceed `bg_subtract_threshold` on its own.
- **`ImportError: datasets`** — `pip install -r scripts/requirements.txt`
  to pick up `datasets`, `huggingface_hub`, and `Pillow`.
