"""Ablate proposed-pipeline stages: accuracy (mean IoU) vs latency, to find
the best configuration under the 200 us / 5000 fps budget."""
import cv2, numpy as np, pandas as pd
import pipelines as P, bench as B
cv2.setNumThreads(1)
meta = B.load()

VARIANTS = {
    "full (clahe+bilat+tophat)":      dict(use_clahe=True,  smooth="bilateral", enhance="tophat"),
    "no-clahe (bilat+tophat)":        dict(use_clahe=False, smooth="bilateral", enhance="tophat"),
    "no-bilat (clahe+tophat)":        dict(use_clahe=True,  smooth="none",      enhance="tophat"),
    "clahe+gauss+tophat":             dict(use_clahe=True,  smooth="gaussian",  enhance="tophat"),
    "gauss+tophat (no clahe)":        dict(use_clahe=False, smooth="gaussian",  enhance="tophat"),
    "median+tophat (no clahe)":       dict(use_clahe=False, smooth="median",    enhance="tophat"),
    "tophat only":                    dict(use_clahe=False, smooth="none",      enhance="tophat"),
    "clahe+bilat+dog":                dict(use_clahe=True,  smooth="bilateral", enhance="dog"),
    "gauss+dog (no clahe)":           dict(use_clahe=False, smooth="gaussian",  enhance="dog"),
    "clahe+gauss+otsu (no enhance)":  dict(use_clahe=True,  smooth="gaussian",  enhance="none"),
    "gauss+otsu (no clahe,no enh)":   dict(use_clahe=False, smooth="gaussian",  enhance="none"),
}

rows = []
for name, cfg in VARIANTS.items():
    d = B.evaluate(meta, P.proposed_pipeline, cfg)
    t = B.time_pipeline(meta, P.proposed_pipeline, cfg)
    rows.append(dict(variant=name, iou=d.iou.mean(), dice=d.dice.mean(),
                     det=d.detected.mean(), us=t["median_ms"]*1000, fps=t["fps"]))
res = pd.DataFrame(rows).sort_values("iou", ascending=False)
pd.set_option("display.width", 140)
print(res.to_string(index=False,
      formatters={"iou": "{:.3f}".format, "dice": "{:.3f}".format,
                  "det": "{:.2%}".format, "us": "{:.0f}".format, "fps": "{:.0f}".format}))
print("\nBudget: 5000 fps = 200 us/frame (single thread, filled, incl findContours).")
