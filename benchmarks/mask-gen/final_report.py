"""Final comparison: current vs proposed pipelines against mask_gt.
Emits a results table (CSV + stdout) and a qualitative montage."""
import cv2, numpy as np, pandas as pd
import pipelines as P, bench as B
cv2.setNumThreads(1)
meta = B.load()

CONFIGS = [
    ("current (default th=8)",          P.current_pipeline, dict()),
    ("current (best-tuned th=2)",       P.current_pipeline, dict(bg_subtract_threshold=2)),
    ("proposed FULL (literal spec)",    P.proposed_pipeline, P.PROPOSED_FULL_CFG),
    ("proposed OPT (tuned, rect-tophat)", P.proposed_pipeline, P.PROPOSED_OPT_CFG),
    ("proposed LEAN (absdiff+Otsu)",    P.proposed_pipeline, P.PROPOSED_LEAN_CFG),
]

rows = []
detail = {}
for name, fn, cfg in CONFIGS:
    d = B.evaluate(meta, fn, cfg)
    t = B.time_pipeline(meta, fn, cfg, n_imgs=40, reps=30)
    detail[name] = d
    rows.append(dict(
        pipeline=name, mean_IoU=d.iou.mean(), median_IoU=d.iou.median(),
        mean_Dice=d.dice.mean(), det_at_0p5=d.detected.mean(),
        IoU_ge_0p7=(d.iou >= 0.7).mean(),
        cell_IoU=d[d.cls == "cell"].iou.mean(),
        cluster_IoU=d[d.cls == "cluster"].iou.mean(),
        us_per_frame=t["median_ms"]*1000, fps=t["fps"]))
res = pd.DataFrame(rows)
res.to_csv("results.csv", index=False)

pd.set_option("display.width", 200)
print(res.to_string(index=False, formatters={
    "mean_IoU":"{:.3f}".format, "median_IoU":"{:.3f}".format, "mean_Dice":"{:.3f}".format,
    "det_at_0p5":"{:.1%}".format, "IoU_ge_0p7":"{:.1%}".format,
    "cell_IoU":"{:.3f}".format, "cluster_IoU":"{:.3f}".format,
    "us_per_frame":"{:.0f}".format, "fps":"{:.0f}".format}))
print("\nN =", len(meta), "detections (144 cell, 29 cluster) | budget 5000 fps = 200 us/frame")

# Qualitative montage: image / current-best / proposed-opt / GT  (error-coded)
def overlay(g, m, gt):
    ov = cv2.cvtColor(g, cv2.COLOR_GRAY2BGR)
    ov[(m > 0) & (gt > 0)] = (0, 200, 0)     # hit
    ov[(gt > 0) & (m == 0)] = (0, 0, 230)    # miss
    ov[(m > 0) & (gt == 0)] = (0, 220, 220)  # false pos
    return ov

sample = meta.sort_values("gt_pix").iloc[[20, 55, 85, 110, 140, 165]]
tiles = []
for _, r in sample.iterrows():
    g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE); gt = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
    bg = P.estimate_background(g)
    mc = P.fill_contours(P.current_pipeline(g, bg, dict(bg_subtract_threshold=2)))
    mp = P.fill_contours(P.proposed_pipeline(g, bg, P.PROPOSED_OPT_CFG))
    col = np.vstack([cv2.cvtColor(g, cv2.COLOR_GRAY2BGR), overlay(g, mc, gt),
                     overlay(g, mp, gt), cv2.cvtColor(gt, cv2.COLOR_GRAY2BGR)])
    tiles.append(col); tiles.append(np.full((col.shape[0], 6, 3), 90, np.uint8))
cv2.imwrite("comparison.png", np.hstack(tiles))
print("\nWrote results.csv and comparison.png")
print("montage rows: image / current(best) / proposed(opt) / GT   "
      "(green=hit, red=missed, yellow=false-pos)")
