"""Benchmark current vs proposed segmentation pipeline against mask_gt.

Accuracy: per-detection IoU / Dice inside a padded bbox crop (isolates the
detection when a strip carries more than one cell).
Speed: median per-frame latency over repeated runs on the real 512x96 strips.
"""
import time
import numpy as np
import pandas as pd
import cv2
import pipelines as P


def load(meta_path="meta.pkl"):
    return pd.read_pickle(meta_path)


def iou_dice(pred, gt):
    p = pred > 0
    g = gt > 0
    inter = np.logical_and(p, g).sum()
    union = np.logical_or(p, g).sum()
    ps, gs = p.sum(), g.sum()
    iou = inter / union if union else 1.0
    dice = 2 * inter / (ps + gs) if (ps + gs) else 1.0
    return iou, dice, int(inter), int(ps), int(gs)


def crop_bbox(arr, bb, pad, shape):
    h, w = shape
    x0 = max(0, int(bb[0]) - pad); y0 = max(0, int(bb[1]) - pad)
    x1 = min(w, int(bb[2]) + pad); y1 = min(h, int(bb[3]) + pad)
    return arr[y0:y1, x0:x1]


def evaluate(meta, pipe_fn, cfg, pad=6, fill=True):
    rows = []
    for _, r in meta.iterrows():
        gray = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        gt = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        bg = P.estimate_background(gray)
        m = pipe_fn(gray, bg, cfg)
        if fill:
            m = P.fill_contours(m)
        bb = (r.bx0, r.by0, r.bx1, r.by1)
        pc = crop_bbox(m, bb, pad, gray.shape)
        gc = crop_bbox(gt, bb, pad, gray.shape)
        iou, dice, inter, ps, gs = iou_dice(pc, gc)
        rows.append(dict(idx=r.idx, cls=r.cls, iou=iou, dice=dice,
                         detected=int(iou > 0.5), inter=inter, pred_px=ps, gt_px=gs))
    return pd.DataFrame(rows)


def summarize(name, df):
    print(f"\n== {name} ==")
    print(f"  mean IoU   {df.iou.mean():.3f}   median {df.iou.median():.3f}")
    print(f"  mean Dice  {df.dice.mean():.3f}   median {df.dice.median():.3f}")
    print(f"  detected@0.5 {df.detected.mean()*100:5.1f}%   "
          f"IoU>=0.7 {(df.iou>=0.7).mean()*100:4.1f}%   IoU<0.3 {(df.iou<0.3).mean()*100:4.1f}%")
    for c in sorted(df.cls.unique()):
        sub = df[df.cls == c]
        print(f"    [{c:8s} n={len(sub):3d}] IoU {sub.iou.mean():.3f}  Dice {sub.dice.mean():.3f}  det {sub.detected.mean()*100:.0f}%")
    return df.iou.mean()


def time_pipeline(meta, pipe_fn, cfg, fill=True, n_imgs=40, reps=30):
    """Median per-frame latency (ms) with background precomputed (production
    stores a captured bg, so bg estimation is amortised out)."""
    sample = meta.drop_duplicates("image").head(n_imgs)
    prepared = []
    for _, r in sample.iterrows():
        g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        prepared.append((g, P.estimate_background(g)))
    # warmup
    for g, bg in prepared:
        m = pipe_fn(g, bg, cfg)
        if fill: P.fill_contours(m)
    times = []
    for _ in range(reps):
        for g, bg in prepared:
            t0 = time.perf_counter()
            m = pipe_fn(g, bg, cfg)
            if fill: P.fill_contours(m)
            times.append((time.perf_counter() - t0) * 1e3)
    times = np.array(times)
    return dict(median_ms=float(np.median(times)),
                p95_ms=float(np.percentile(times, 95)),
                fps=1000.0 / float(np.median(times)))


if __name__ == "__main__":
    meta = load()
    cur_cfg = {}
    prop_cfg = dict(enhance="tophat")
    dc = evaluate(meta, P.current_pipeline, cur_cfg)
    dp = evaluate(meta, P.proposed_pipeline, prop_cfg)
    summarize("CURRENT", dc)
    summarize("PROPOSED (tophat, default)", dp)
    print("\nLatency (single-thread, 512x96, filled):")
    print("  current ", time_pipeline(meta, P.current_pipeline, cur_cfg))
    print("  proposed", time_pipeline(meta, P.proposed_pipeline, prop_cfg))
