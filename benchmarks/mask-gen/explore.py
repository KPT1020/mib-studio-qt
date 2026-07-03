"""Exploratory experiments beyond the headline comparison:

  A. LEAN vs OPT under background drift  (why keep the top-hat)
  B. auto_refine_band: recover the channel band from a coarse/sloppy ROI
  C. alternative methods: Triangle vs Otsu threshold, adaptive threshold,
     hole-filling, and connectedComponents vs findContours (speed)
"""
import time
import cv2
import numpy as np
import pandas as pd
import pipelines as P
import bench as B
cv2.setNumThreads(1)
meta = B.load()
BB = lambda r: (r.bx0, r.by0, r.bx1, r.by1)


def score(cfg, bandfn=None, framefn=None, bgfn=None):
    ious = []
    for _, r in meta.iterrows():
        g0 = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        gt = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        bg = (bgfn or P.estimate_background)(g0)
        g = framefn(g0) if framefn else g0
        c = dict(cfg)
        if bandfn:
            c["roi"] = bandfn(g)
        m = P.fill_contours(P.proposed_pipeline(g, bg, c))
        pc = B.crop_bbox(m, BB(r), 6, g.shape)
        gc = B.crop_bbox(gt, BB(r), 6, g.shape)
        iou, *_ = B.iou_dice(pc, gc)
        ious.append(iou)
    ious = np.array(ious)
    return ious.mean(), (ious > 0.5).mean()


def drift_frame(g0):
    h, w = g0.shape
    ramp = np.tile(np.linspace(-14, 14, w), (h, 1))
    return np.clip(g0.astype(np.int16) + ramp, 0, 255).astype(np.uint8)


print("A. Background-drift robustness (stale bg + illumination ramp)")
for name, cfg in [("LEAN (no top-hat)", P.PROPOSED_LEAN_CFG),
                  ("OPT  (rect top-hat)", P.PROPOSED_OPT_CFG),
                  ("OPT+adaptive thresh", dict(P.PROPOSED_OPT_CFG, threshold="adaptive"))]:
    ci, cd = score(cfg)
    di, dd = score(cfg, framefn=drift_frame)
    print(f"   {name:22s} clean {ci:.3f}/{cd:.0%}   drifted {di:.3f}/{dd:.0%}")

print("\nB. auto_refine_band vs fixed 12..70")
for name, bandfn in [("fixed 12..70", lambda g: (12, 70)),
                     ("auto (full frame)", lambda g: P.auto_refine_band(g)),
                     ("auto (sloppy 5..90)", lambda g: P.auto_refine_band(g, (5, 90))),
                     ("auto (margin=5)", lambda g: P.auto_refine_band(g, margin=5))]:
    i, d = score(P.PROPOSED_OPT_CFG, bandfn=bandfn)
    print(f"   {name:22s} IoU {i:.3f}  det {d:.0%}")

print("\nC. Alternative methods (OPT base, fixed band)")
variants = {
    "Otsu (baseline)":        dict(P.PROPOSED_OPT_CFG),
    "Triangle threshold":     dict(P.PROPOSED_OPT_CFG, threshold="triangle"),
    "Otsu + fill_holes":      dict(P.PROPOSED_OPT_CFG, fill_holes=True),
    "Triangle + fill_holes":  dict(P.PROPOSED_OPT_CFG, threshold="triangle", fill_holes=True),
    "Adaptive (no top-hat)":  dict(P.PROPOSED_LEAN_CFG, threshold="adaptive"),
}
for name, cfg in variants.items():
    i, d = score(cfg)
    t = B.time_pipeline(meta, P.proposed_pipeline, cfg, n_imgs=40, reps=20)
    print(f"   {name:22s} IoU {i:.3f}  det {d:.0%}  {t['median_ms']*1000:4.0f} us  {t['fps']:5.0f} fps")

# connectedComponents vs findContours (label/measure cost only)
print("\nD. Contour extraction cost: findContours vs connectedComponentsWithStats")
masks = [P.proposed_pipeline(cv2.imread(f, cv2.IMREAD_GRAYSCALE),
         P.estimate_background(cv2.imread(f, cv2.IMREAD_GRAYSCALE)), P.PROPOSED_OPT_CFG)
         for f in meta.image.unique()[:40]]
def t_us(fn, reps=50):
    ts = []
    for _ in range(reps):
        for m in masks:
            t0 = time.perf_counter(); fn(m); ts.append((time.perf_counter() - t0) * 1e6)
    return np.median(ts)
print(f"   findContours+fill              {t_us(P.fill_contours):5.1f} us")
print(f"   connectedComponentsWithStats   {t_us(lambda m: cv2.connectedComponentsWithStats(m, 8)):5.1f} us")
