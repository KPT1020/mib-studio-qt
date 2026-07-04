"""Performance profile (NOT accuracy) of reconstructing the inner contour from a
local Laplacian-variance ridge, as a topology-free replacement for the naturally
nested RETR_TREE inner contour used for size fidelity.

We measure only cost here. The question is: how many microseconds per frame does
the reconstruction add on top of the existing OPT segmentation, and does it fit
the ~180-200 us / 5000 fps budget from REPORT.md?

Method under test (per detection):
  1. Laplacian(gray, CV_32F, ksize=3)                 -- gradient/edge response
  2. local variance of Laplacian via two box filters  -- E[L^2] - E[L]^2 ridge map
  3. threshold the ridge band (Otsu on the normalized map)
  4. AND with the outer-contour interior mask
  5. findContours(RETR_CCOMP) -> inner contour = the hole

Two scopes are timed because they cost very differently:
  * BAND  -- run over the whole ROI band (58x512) every frame (worst case)
  * BBOX  -- run only inside the detected outer bbox (the realistic fallback,
             triggered only when the native nested contour is absent)

For budget context we also time:
  * OPT segmentation (proposed_pipeline, PROPOSED_OPT_CFG) -- the baseline stage
  * native RETR_TREE inner-contour extraction              -- what we do today
"""
import time
import numpy as np
import pandas as pd
import cv2
import pipelines as P

ROI = (P.ROI_Y0, P.ROI_Y1)
VAR_WIN = 5          # local-variance window for the Laplacian ridge
RIDGE_PAD = 4        # bbox padding for the bbox-scoped reconstruction


def _median_us(fn, reps=50):
    fn()  # warmup
    ts = []
    for _ in range(reps):
        t0 = time.perf_counter()
        fn()
        ts.append((time.perf_counter() - t0) * 1e6)
    a = np.array(ts)
    return float(np.median(a)), float(np.percentile(a, 95))


# --- reconstruction primitives, timed as a whole and per-stage ---------------
def lap_variance_map(sub):
    lap = cv2.Laplacian(sub, cv2.CV_32F, ksize=3)
    m = cv2.boxFilter(lap, cv2.CV_32F, (VAR_WIN, VAR_WIN))
    m2 = cv2.boxFilter(lap * lap, cv2.CV_32F, (VAR_WIN, VAR_WIN))
    return m2 - m * m


def ridge_band(var):
    varn = cv2.normalize(var, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)
    _, th = cv2.threshold(varn, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    return th


def reconstruct_inner(sub, outer_sub):
    """Full reconstruction on a crop. Returns the inner contour (or None)."""
    var = lap_variance_map(sub)
    ridge = ridge_band(var)
    ridge = cv2.bitwise_and(ridge, outer_sub)
    cnts, hier = cv2.findContours(ridge, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
    if hier is None:
        return None
    hier = hier[0]
    best, best_a = None, 0.0
    for i, c in enumerate(cnts):
        if hier[i][3] != -1:  # a hole = child = candidate inner boundary
            a = cv2.contourArea(c)
            if a > best_a:
                best, best_a = c, a
    return best


# --- per-detection fixtures: real strip, OPT outer mask + bbox ---------------
def build_fixtures(meta, n=60):
    fx = []
    for _, r in meta.drop_duplicates("image").head(n).iterrows():
        gray = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        bg = P.estimate_background_fast(gray)
        seg = P.proposed_pipeline(gray, bg, P.PROPOSED_OPT_CFG)  # binary rim mask
        cnts, _ = cv2.findContours(seg, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        cnts = [c for c in cnts if cv2.contourArea(c) >= 10]
        if not cnts:
            continue
        c = max(cnts, key=cv2.contourArea)
        x, y, w, h = cv2.boundingRect(c)
        # padded bbox crop within the ROI band
        y0b = max(ROI[0], y - RIDGE_PAD); y1b = min(ROI[1], y + h + RIDGE_PAD)
        x0b = max(0, x - RIDGE_PAD); x1b = min(gray.shape[1], x + w + RIDGE_PAD)
        outer = np.zeros_like(gray)
        cv2.drawContours(outer, [c], -1, 255, cv2.FILLED)
        fx.append(dict(
            gray=gray, bg=bg,
            band_gray=gray[ROI[0]:ROI[1]].copy(),
            band_outer=outer[ROI[0]:ROI[1]].copy(),
            bbox_gray=gray[y0b:y1b, x0b:x1b].copy(),
            bbox_outer=outer[y0b:y1b, x0b:x1b].copy(),
            bbox_wh=(x1b - x0b, y1b - y0b), seg=seg))
    return fx


def profile(fx):
    band_px = np.mean([f["band_gray"].size for f in fx])
    bbox_px = np.mean([f["bbox_gray"].size for f in fx])
    print(f"fixtures: {len(fx)} strips | ROI band ~{band_px:.0f}px "
          f"({fx[0]['band_gray'].shape}) | mean bbox ~{bbox_px:.0f}px")

    def over(fn_of_fixture):
        # median across a full sweep of all fixtures, so per-frame numbers are
        # the median single-detection cost (not one strip repeated).
        def run():
            for f in fx:
                fn_of_fixture(f)
        med, p95 = _median_us(run, reps=30)
        return med / len(fx), p95 / len(fx)

    rows = []

    def add(name, fn):
        med, _ = over(fn)
        rows.append((name, med))

    # budget-context baselines
    add("OPT segmentation (baseline stage)",
        lambda f: P.proposed_pipeline(f["gray"], f["bg"], P.PROPOSED_OPT_CFG))
    add("native RETR_TREE inner extract (today)",
        lambda f: cv2.findContours(f["seg"], cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE))

    # reconstruction -- full method, two scopes
    add("RECON full @ BAND (58x512)",
        lambda f: reconstruct_inner(f["band_gray"], f["band_outer"]))
    add("RECON full @ BBOX (per-detection)",
        lambda f: reconstruct_inner(f["bbox_gray"], f["bbox_outer"]))

    # reconstruction -- per-stage breakdown at BBOX scope
    add("  stage: Laplacian",
        lambda f: cv2.Laplacian(f["bbox_gray"], cv2.CV_32F, ksize=3))
    add("  stage: variance map (Lap + 2 box)",
        lambda f: lap_variance_map(f["bbox_gray"]))
    add("  stage: ridge threshold (norm+Otsu)",
        lambda f: ridge_band(lap_variance_map(f["bbox_gray"])))
    add("  stage: AND + findContours(CCOMP)",
        lambda f: cv2.findContours(
            cv2.bitwise_and(ridge_band(lap_variance_map(f["bbox_gray"])), f["bbox_outer"]),
            cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE))

    print(f"\n{'stage':<44}{'us/detection (median)':>22}")
    print("-" * 66)
    for name, med in rows:
        print(f"{name:<44}{med:>18.1f}")
    return dict(rows)


if __name__ == "__main__":
    meta = pd.read_pickle("meta.pkl")
    fx = build_fixtures(meta)
    r = profile(fx)
    base = r["OPT segmentation (baseline stage)"]
    band = r["RECON full @ BAND (58x512)"]
    bbox = r["RECON full @ BBOX (per-detection)"]
    print("\n--- budget context (REPORT.md: ~180 us/frame => 5560 fps) ---")
    print(f"OPT segmentation alone         : {base:6.1f} us   ({1e6/base:.0f} fps)")
    print(f"+ recon @ BAND (every frame)   : {base+band:6.1f} us   ({1e6/(base+band):.0f} fps)  "
          f"(+{band:.1f}, {100*band/base:.0f}% over baseline)")
    print(f"+ recon @ BBOX (every frame)   : {base+bbox:6.1f} us   ({1e6/(base+bbox):.0f} fps)  "
          f"(+{bbox:.1f}, {100*bbox/base:.0f}% over baseline)")
    # The reconstruction only FIRES when the native nested contour is absent.
    # REPORT.md: native ring present on 66-73% of frames -> miss rate ~0.27-0.34.
    for miss in (0.30, 0.34):
        amort = bbox * miss
        print(f"+ recon @ BBOX, fallback fires {miss*100:.0f}% : "
              f"{base+amort:6.1f} us   ({1e6/(base+amort):.0f} fps)  "
              f"(amortised +{amort:.1f})")
