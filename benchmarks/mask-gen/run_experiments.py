"""Run every method prototype against the same GT and record results to
results/experiments.{json,csv} for later comparison.

Groups:
  1 background models (single-frame estimators + temporal MOG2/KNN)
  2 hysteresis threshold
  3 distance-transform watershed
  4 shape regularization
  5 throughput paths
  6 distilled CNN  -- recorded as NOT RUN (needs training infra)
"""
import os, json, time
import cv2, numpy as np, pandas as pd
import pipelines as P, bench as B, methods as M
cv2.setNumThreads(1)
meta = B.load()
OPT = P.PROPOSED_OPT_CFG
rows = []


def rec(group, method, r=None, us=None, notes=""):
    d = dict(group=group, method=method, iou=None, det=None,
             iou_cell=None, iou_cluster=None, us=us,
             fps=(1e6 / us if us else None), notes=notes)
    if r:
        d.update({k: round(r[k], 4) for k in r})
    rows.append(d)
    tag = f"iou {r['iou']:.3f} det {r['det']:.0%}" if r else ""
    print(f"  [{group}] {method:32s} {tag}  {('%.0f us'%us) if us else ''}  {notes}")


def drift(g0):
    h, w = g0.shape
    ramp = np.tile(np.linspace(-14, 14, w), (h, 1))
    return np.clip(g0.astype(np.int16) + ramp, 0, 255).astype(np.uint8)


print("== baseline ==")
b_us = M.latency_us(meta, P.proposed_pipeline, OPT)
rec("baseline", "OPT (absdiff+tophat+Otsu+close)", M.score(meta, P.proposed_pipeline, OPT), b_us, "reference")
rec("baseline", "OPT drifted-bg", M.score(meta, P.proposed_pipeline, OPT, framefn=drift), notes="stale bg + shading")

print("== (1) background models: clean / drift ==")
BGS = [("row-median (current)", M.bg_row_median), ("row-mean", M.bg_row_mean),
       ("morph-open k25", M.bg_morph_open), ("gaussian k51", M.bg_gaussian),
       ("poly surface o2", M.bg_poly)]
for name, fn in BGS:
    rec("1-bg-clean", name, M.score(meta, P.proposed_pipeline, OPT, bgfn=fn))
for name, fn in BGS:
    rec("1-bg-drift", name, M.score(meta, P.proposed_pipeline, OPT, bgfn=fn, framefn=drift))
for kind in ["MOG2", "KNN"]:
    rec("1-temporal", kind + " clean", M.temporal_eval(meta, kind), notes="synth clean history")
    rec("1-temporal", kind + " drift, stale history", M.temporal_eval(meta, kind, framefn=drift), notes="history pre-drift")
    rec("1-temporal", kind + " drift, adapted history", M.temporal_eval(meta, kind, framefn=drift, drift_history=True), notes="continuous adaptation")
# temporal per-frame latency (apply + close/open), warm model
def mog2_us(kind="MOG2", reps=30):
    sample = [cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)[P.ROI_Y0:P.ROI_Y1]
              for _, r in meta.drop_duplicates("image").head(40).iterrows()]
    sub = (cv2.createBackgroundSubtractorMOG2 if kind == "MOG2"
           else cv2.createBackgroundSubtractorKNN)(history=8, detectShadows=False)
    for g in sample: sub.apply(g)
    ker = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    ts = []
    for _ in range(reps):
        for g in sample:
            t0 = time.perf_counter()
            fg = sub.apply(g, learningRate=0)
            cv2.morphologyEx(fg, cv2.MORPH_CLOSE, ker)
            ts.append((time.perf_counter() - t0) * 1e6)
    return float(np.median(ts))
rec("1-temporal", "MOG2 per-frame latency", None, mog2_us("MOG2"), "apply+close, warm model; OVER budget")
# cheap EWMA running-average background (budget-friendly temporal)
rec("1-temporal", "EWMA bg clean", M.ewma_temporal_eval(meta), notes="running-avg bg + OPT")
rec("1-temporal", "EWMA bg drift, adapted", M.ewma_temporal_eval(meta, framefn=drift, drift_history=True), notes="adapts to drift, ~absdiff cost")
rec("1-temporal", "EWMA bg drift, stale", M.ewma_temporal_eval(meta, framefn=drift), notes="no adaptation")

print("== (2) hysteresis threshold ==")
for lo in [0.4, 0.5, 0.6]:
    cfg = dict(OPT, lo_scale=lo, hi_scale=1.0)
    rec("2-hysteresis", f"hi=1.0 lo={lo}", M.score(meta, M.hysteresis_pipeline, cfg),
        M.latency_us(meta, M.hysteresis_pipeline, cfg))
rec("2-hysteresis", "hi=1.0 lo=0.5 DRIFT", M.score(meta, M.hysteresis_pipeline, dict(OPT, lo_scale=0.5), framefn=drift))

print("== (3) watershed ==")
for pf in [0.4, 0.5, 0.6]:
    cfg = dict(peak_frac=pf)
    rec("3-watershed", f"peak_frac={pf}", M.score(meta, M.watershed_pipeline, cfg),
        M.latency_us(meta, M.watershed_pipeline, cfg), "clusters=single-object GT")
# multi-detection strips only
multi = meta.groupby("image").filter(lambda d: len(d) > 1)
rec("3-watershed", "OPT on multi-det strips", M.score(multi, P.proposed_pipeline, OPT), notes=f"n={len(multi)}")
rec("3-watershed", "watershed on multi-det strips", M.score(multi, M.watershed_pipeline, dict(peak_frac=0.5)), notes=f"n={len(multi)}")

print("== (4) shape regularization ==")
for mode in ["ellipse", "hull"]:
    cfg = dict(shape=mode)
    rec("4-shape", mode, M.score(meta, M.shape_pipeline, cfg), M.latency_us(meta, M.shape_pipeline, cfg))

print("== (5) throughput ==")
rec("5-throughput", "single-thread (1 core)", None, b_us, "baseline")
cv2.setNumThreads(0)
rec("5-throughput", "OpenCV threads=all", None, M.latency_us(meta, P.proposed_pipeline, OPT), "cv2.setNumThreads(0)")
cv2.setNumThreads(1)
# strip batching: linear stages amortized over K stacked strips (Otsu stays per-strip)
sample = [cv2.imread(r.image, cv2.IMREAD_GRAYSCALE) for _, r in meta.drop_duplicates("image").head(32).iterrows()]
bgs = [M.bg_row_median(g) for g in sample]
y0, y1 = P.ROI_Y0, P.ROI_Y1
K = 16
def batched_once():
    big = np.vstack([g[y0:y1] for g in sample[:K]]); bigbg = np.vstack([b[y0:y1] for b in bgs[:K]])
    d = cv2.absdiff(big, bigbg)
    d = cv2.morphologyEx(d, cv2.MORPH_TOPHAT, cv2.getStructuringElement(cv2.MORPH_RECT, (21, 21)))
    cv2.morphologyEx(d, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))
for _ in range(5): batched_once()
ts = []
for _ in range(30):
    t0 = time.perf_counter(); batched_once(); ts.append((time.perf_counter() - t0) * 1e6 / K)
rec("5-throughput", f"strip-batching K={K} (linear stages)", None, float(np.median(ts)), "per-frame amortized; Otsu still per-strip")
rec("5-throughput", "GPU/CUDA", None, None, f"{cv2.cuda.getCudaEnabledDeviceCount()} CUDA devices - not available")

print("== (6) distilled CNN ==")
rec("6-cnn", "tiny U-Net distilled from SAM2", None, None, "NOT RUN - needs training pipeline + GPU; separate track")

os.makedirs("results", exist_ok=True)
df = pd.DataFrame(rows)
df.to_csv("results/experiments.csv", index=False)
json.dump(rows, open("results/experiments.json", "w"), indent=2)
print(f"\nWrote results/experiments.csv and results/experiments.json ({len(rows)} rows)")
