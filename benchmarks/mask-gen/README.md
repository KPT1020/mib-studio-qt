# Mask-generation pipeline benchmark

Compares the production segmentation pipeline
(`ProcessingService::computeProcessedFrame`) against a proposed pipeline
(`absdiff → CLAHE → bilateral → DoG/Top-hat → Otsu → close → findContours`)
on the `gavinlouuu/biowork-mask-gen-benchmark` dataset, scoring both against
the SAM2-derived reference masks (`mask_gt`).

Findings and the recommended real-time config are in [`REPORT.md`](REPORT.md).

## Run

```bash
python3 -m venv venv && . venv/bin/activate
pip install -r requirements.txt

export HF_ACCESS_TOKEN=hf_...      # read access to the (private) dataset
python download_gt.py              # -> cache/, bench.parquet, meta.pkl
python final_report.py             # -> results.csv, comparison.png (headline table)

python ablation.py                 # per-stage accuracy/latency ablation
python tune.py                     # parameter sweep for the tuned config
python explore.py                  # drift robustness, auto-refine, alt thresholds
python run_experiments.py          # all method prototypes -> results/experiments.*

python download_stream.py          # 5000-frame stream + GT->stream index match
python stream_bench.py             # real-stream validation -> results/stream_experiments.*
```

Note: the temporal-background tests (MOG2/KNN/EWMA) synthesise a per-strip
history because this GT set is independent frames, not video; see the fairness
note in `methods.temporal_eval` and REPORT.md.

## Files

| File | Purpose |
|------|---------|
| `download_gt.py`  | Resolve dataset images + `mask_gt` from HF, build `meta.pkl` |
| `pipelines.py`    | Both pipelines + named configs (`PROPOSED_OPT_CFG`, …) + background estimator |
| `bench.py`        | IoU/Dice scoring (per-detection bbox crop) + latency timing |
| `final_report.py` | Headline comparison table + `comparison.png` montage |
| `ablation.py`     | Which stages earn their cost (CLAHE / bilateral / DoG / top-hat) |
| `tune.py`         | Top-hat kernel/shape, Otsu scale, morphology sweep |
| `explore.py`      | Drift robustness, `auto_refine_band`, alternative thresholds |
| `methods.py`      | Prototype methods: background models, hysteresis, watershed, shape reg |
| `run_experiments.py` | Runs every prototype, writes `results/experiments.{csv,json}` |
| `download_stream.py` | Fetch the 5000-frame stream, match GT frames by pixel hash |
| `stream_bench.py` | **Real-stream** background + throughput validation (authoritative) |
| `focus_metric.py` | Robust focus metric (Laplacian variance) vs the nested-contour ring ratio |
| `visualize_focus.py` | Build `results/focus_data.csv` + ranked-cell montage and analysis figures |
| `scatter_focus.py` | Light-scattering focus signature on real cells (dark disc + bright rim, signed rim contrast, focus-energy vs ring strength) → `results/scatter_focus_*` |
| `fetch_resilient.py` | Retry/resume GT fetch (tolerates transient LFS resets); builds `meta.pkl` like `download_gt.py` |
| `results/`        | Recorded experiment results for later comparison |

## Method notes

* **Background** — this geometry has a strong per-row brightness gradient
  (channel walls) but is flat along the flow axis, so the empty-channel
  background is estimated as the per-row median across columns. In the live
  system this is replaced by the captured/rolling background frame, so the
  absdiff reference is even cheaper there.
* **ROI band** — cells occupy the interior plateau (rows ~12–70). Restricting
  work to that band removes the bright frame edges and textured walls that
  otherwise create false positives and poison the global Otsu histogram. This
  is the single biggest accuracy lever (mean IoU 0.43 → 0.73 before tuning).
* **Scoring** — each detection is scored inside its (padded) bounding box so
  multi-cell strips don't cross-contaminate; masks are contour-filled to match
  the filled reference masks.
* **Latency** — median over repeated single-thread runs on the real 512×96
  strips, background precomputed (production stores it), including
  `findContours` + fill. Same OpenCV kernels as the C++ app; treat as
  indicative for the C++ path, which has no per-call Python overhead.
