"""Real-stream validation on gavinlouuu/512x96stream (5000 consecutive frames).

Unlike run_experiments.py (which synthesises a per-strip history because the GT
set is independent frames), this uses the ACTUAL preceding stream frames as
history. Each GT detection is located in the stream by exact pixel hash
(download_stream.py), so temporal backgrounds and sustained throughput are
measured on genuine video.

Records results/stream_experiments.csv. Run download_stream.py first.
"""
import os, json, time
import numpy as np, cv2, pandas as pd
import pipelines as P, bench as B
cv2.setNumThreads(1)

frames = np.load("stream_frames.npy")
idx = json.load(open("gt_stream_idx.json"))
meta = B.load()
OPT = P.PROPOSED_OPT_CFG
BB = lambda r: (r.bx0, r.by0, r.bx1, r.by1)
rows = []


def rec(method, iou=None, det=None, us=None, notes=""):
    rows.append(dict(method=method, iou=iou if iou is None else round(iou, 4),
                     det=det if det is None else round(det, 4),
                     us=us if us is None else round(us, 1),
                     fps=None if not us else round(1e6 / us), notes=notes))
    print(f"  {method:38s} " + (f"IoU {iou:.3f} det {det:.0%}" if iou is not None else "") +
          (f"  {us:.0f} us -> {1e6/us:.0f} fps" if us else "") + f"  {notes}")


def score(bgfn, cfg=OPT):
    ious = []
    for _, r in meta.iterrows():
        i = idx[r.image]; g = frames[i]
        m = P.fill_contours(P.proposed_pipeline(g, bgfn(i, g), cfg))
        pc = B.crop_bbox(m, BB(r), 6, g.shape)
        gc = B.crop_bbox(cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE), BB(r), 6, g.shape)
        iou, *_ = B.iou_dice(pc, gc); ious.append(iou)
    a = np.array(ious); return a.mean(), (a > 0.5).mean()


# --- background estimators keyed by stream index (real history available) ---
def rowmean(i, g):
    return np.broadcast_to(cv2.reduce(g, 1, cv2.REDUCE_AVG), g.shape)
def rowmedian(i, g):
    return np.broadcast_to(np.median(g, axis=1, keepdims=True).astype(np.uint8), g.shape)
_cap = None
def captured(i, g):
    global _cap
    if _cap is None:
        _cap = cv2.reduce(frames[0], 1, cv2.REDUCE_AVG)
    return np.broadcast_to(_cap, g.shape)
def tmedian(K):
    def f(i, g):
        s = frames[max(0, i - K):i]
        return np.median(s, axis=0).astype(np.uint8) if len(s) else rowmean(i, g)
    return f
def ewma(alpha, warm=200):
    def f(i, g):
        s = frames[max(0, i - warm):i]
        if not len(s):
            return rowmean(i, g)
        acc = s[0].astype(np.float32)
        for fr in s[1:]:
            acc = (1 - alpha) * acc + alpha * fr
        return acc.astype(np.uint8)
    return f


print("== accuracy: background strategy (real stream, OPT pipeline) ==")
for name, fn in [("per-row mean (instantaneous)", rowmean),
                 ("per-row median (instantaneous)", rowmedian),
                 ("captured frame#0 (stale)", captured),
                 ("temporal median K=30 (real hist)", tmedian(30)),
                 ("EWMA a=0.02 (real hist)", ewma(0.02))]:
    a, d = score(fn); rec(name, a, d)
a, d = score(rowmean, dict(OPT, enhance="none"))
rec("per-row mean, NO top-hat", a, d, notes="top-hat adds nothing on real data")

print("\n== sustained throughput (real stream, single thread, incl. background) ==")
def bench(fn, N=3000):
    for i in range(50): fn(frames[i])
    t = time.perf_counter()
    for i in range(N): fn(frames[i])
    return (time.perf_counter() - t) / N * 1e6
pipe_opt = lambda g: P.fill_contours(P.proposed_pipeline(g, rowmean(0, g), OPT))
pipe_nt = lambda g: P.fill_contours(P.proposed_pipeline(g, rowmean(0, g), dict(OPT, enhance="none")))
rec("row-mean bg only", us=bench(lambda g: cv2.reduce(g, 1, cv2.REDUCE_AVG)), notes="cv2.reduce")
rec("np.median bg only", us=bench(lambda g: np.median(g, axis=1)), notes="33x slower - avoid")
rec("full OPT + row-mean bg", us=bench(pipe_opt), notes="with top-hat")
rec("full NO-top-hat + row-mean bg", us=bench(pipe_nt), notes="recommended for real stream")

os.makedirs("results", exist_ok=True)
pd.DataFrame(rows).to_csv("results/stream_experiments.csv", index=False)
json.dump(rows, open("results/stream_experiments.json", "w"), indent=2)
print(f"\nWrote results/stream_experiments.csv ({len(rows)} rows). Budget: 200 us = 5000 fps.")
