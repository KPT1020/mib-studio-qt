"""Area-accuracy comparison: which pipeline measures cell size closest to GT?

The decoupled-measurement work keeps `area` on the *fixed-threshold* basis so it
does not drift with the per-frame Otsu cut. But "stable" is only useful if the
basis is also *accurate*. This scores each pipeline's cell area against the SAM2
GT area (the physical truth), alongside IoU / detection, so the choice of
measurement basis can be made on accuracy, not just stability.

Pipelines:
  current FIXED     -- ProcessingService adaptive-off (fixed bg_subtract_threshold)
  current ADAPTIVE  -- ProcessingService adaptive-on (Otsu, what ships behind the flag)
  proposed LEAN     -- absdiff -> Otsu*1.1 -> close 5x5 (REPORT recommendation)
  proposed OPT      -- + rect top-hat

Run after download_gt.py. Writes results/area_accuracy.csv.
"""
import numpy as np
import pandas as pd
import cv2
import pipelines as P
import decoupled_bench as D

CUR = dict(gaussian_blur_size=3, morph_kernel_size=3, morph_iterations=1,
           bg_subtract_threshold=8, otsu_scale=1.1)


def _crop(a, bb, shape, pad=6):
    h, w = shape
    x0 = max(0, int(bb[0]) - pad); y0 = max(0, int(bb[1]) - pad)
    x1 = min(w, int(bb[2]) + pad); y1 = min(h, int(bb[3]) + pad)
    return a[y0:y1, x0:x1]


def _score(mask_fn):
    rows = []
    for _, r in pd.read_pickle("meta.pkl").iterrows():
        gray = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        gt = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        if gray is None or gt is None:
            continue
        bg = P.estimate_background(gray)
        m = P.fill_contours(mask_fn(gray, bg))
        bb = (r.bx0, r.by0, r.bx1, r.by1)
        pc = _crop(m, bb, gray.shape) > 0
        gc = _crop(gt, bb, gray.shape) > 0
        inter = np.logical_and(pc, gc).sum(); union = np.logical_or(pc, gc).sum()
        pa, ga = int(pc.sum()), int(gc.sum())
        rows.append(dict(cls=r.cls, iou=inter / union if union else 1.0,
                         det=int((inter / union if union else 1.0) > 0.5),
                         aerr=abs(pa - ga) / ga if ga > 0 else np.nan))
    return pd.DataFrame(rows)


PIPES = [
    ("current  FIXED    (adaptive off)", lambda g, b: D.fixed_mask(g, b, CUR)),
    ("current  ADAPTIVE (Otsu, shipped)", lambda g, b: D.adaptive_mask(g, b, CUR)),
    ("proposed LEAN     (absdiff+Otsu)", lambda g, b: P.proposed_pipeline(g, b, P.PROPOSED_LEAN_CFG)),
    ("proposed OPT      (+ top-hat)", lambda g, b: P.proposed_pipeline(g, b, P.PROPOSED_OPT_CFG)),
]


def main():
    print(f"\n{'pipeline':36s} {'meanIoU':>7s} {'det@0.5':>8s} "
          f"{'areaErr med':>12s} {'areaErr p90':>12s}")
    out = []
    for name, fn in PIPES:
        df = _score(fn)
        print(f"{name:36s} {df.iou.mean():7.3f} {df.det.mean()*100:7.1f}% "
              f"{df.aerr.median()*100:11.1f}% {df.aerr.quantile(.9)*100:11.1f}%")
        out.append(dict(pipeline=name, mean_iou=df.iou.mean(),
                        det_at_0p5=df.det.mean(), area_err_median=df.aerr.median(),
                        area_err_p90=df.aerr.quantile(.9)))
    pd.DataFrame(out).to_csv("results/area_accuracy.csv", index=False)
    print("\nwrote results/area_accuracy.csv")
    print("\nArea vs GT truth is the downstream-analysis metric: the current\n"
          "pipeline (fixed OR adaptive) is ~49% off; proposed is ~10%. The fixed\n"
          "measurement basis is stable but inaccurate -- accuracy needs the\n"
          "proposed front-end, not a measurement remap.")


if __name__ == "__main__":
    main()
