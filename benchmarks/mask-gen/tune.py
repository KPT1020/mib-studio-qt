"""Fine-tune the pruned proposed pipeline for max IoU under 200 us."""
import cv2, numpy as np, pandas as pd, itertools
import pipelines as P, bench as B
cv2.setNumThreads(1)
meta = B.load()

# rectangular top-hat uses OpenCV's separable morphology -> much cheaper than ellipse
def add_rect_tophat():
    pass

grid = []
for smooth in ["none", "gaussian"]:
    for gauss_k in ([3] if smooth == "gaussian" else [3]):
        for enhance in ["tophat", "none"]:
            for tk in ([21, 31] if enhance == "tophat" else [15]):
                for shape in (["ellipse", "rect"] if enhance == "tophat" else ["ellipse"]):
                    for oscale in [0.8, 1.0, 1.2]:
                        for oiter in [0, 1]:
                            grid.append(dict(use_clahe=False, smooth=smooth, gauss_k=gauss_k,
                                             enhance=enhance, tophat_kernel=tk, tophat_shape=shape,
                                             otsu_scale=oscale, open_iterations=oiter,
                                             close_iterations=1, morph_kernel_size=3))

rows = []
for cfg in grid:
    d = B.evaluate(meta, P.proposed_pipeline, cfg)
    t = B.time_pipeline(meta, P.proposed_pipeline, cfg, n_imgs=40, reps=15)
    rows.append(dict(iou=d.iou.mean(), dice=d.dice.mean(), det=d.detected.mean(),
                     us=t["median_ms"]*1000, fps=t["fps"],
                     sm=cfg["smooth"], enh=cfg["enhance"], tk=cfg["tophat_kernel"],
                     shape=cfg["tophat_shape"], os=cfg["otsu_scale"], oi=cfg["open_iterations"]))
res = pd.DataFrame(rows)
res["under"] = res.us < 200
print("=== Top 12 by IoU (all) ===")
print(res.sort_values("iou", ascending=False).head(12).to_string(index=False,
      formatters={"iou":"{:.3f}".format,"dice":"{:.3f}".format,"det":"{:.2%}".format,
                  "us":"{:.0f}".format,"fps":"{:.0f}".format}))
print("\n=== Best IoU UNDER 200us budget ===")
print(res[res.under].sort_values("iou", ascending=False).head(8).to_string(index=False,
      formatters={"iou":"{:.3f}".format,"dice":"{:.3f}".format,"det":"{:.2%}".format,
                  "us":"{:.0f}".format,"fps":"{:.0f}".format}))
res.to_pickle("tune_res.pkl")
