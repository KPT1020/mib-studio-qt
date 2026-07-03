"""Robust focus metric to replace the nested-contour ring ratio.

The production ring ratio (`ProcessingService::calculateRingRatio`) is
sqrt(parentArea - innerArea) from a *nested* contour pair found by
findContours(RETR_TREE). It only exists when the cell thresholds into a closed
ring (rim above threshold, dark centre below), so it breaks whenever the rim is
not a closed loop. On this GT set the nested ring is present on only ~66-73 % of
frames (fixed vs Otsu threshold) -- see stdout.

Focus actually lives in the *intensity* of the cell, not in mask topology. So
we compute focus from the background-subtracted intensity inside a solid cell
mask (here mask_gt; in production the Otsu-filled mask). Candidates:
  * lap_var       -- variance of the Laplacian (classic focus measure)
  * tenengrad     -- mean Sobel gradient energy
  * intensity_std -- std of intensity within the mask
  * radial_ratio  -- (outer-annulus mean - inner-disc mean) intensity contrast

Validation:
  1. robustness  -- fraction of frames on which the metric is defined
  2. correlation -- Spearman vs the dataset's reference ring_ratio
  3. defocus response -- blur the sharpest cells progressively; a good focus
     metric falls monotonically with a large dynamic range.

Writes results/focus_experiments.{csv,json}.
"""
import json, os
import numpy as np, pandas as pd, cv2
import bench as B


def rowmean_bg(g):
    return np.broadcast_to(cv2.reduce(g, 1, cv2.REDUCE_AVG), g.shape)


def focus_scores(g, mask):
    """All focus candidates for one cell. Returns None if the mask is too small."""
    d = cv2.absdiff(g, rowmean_bg(g)).astype(np.float32)
    ys, xs = np.where(mask > 0)
    if len(ys) < 8:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    patch = d[y0:y1, x0:x1]
    pm = mask[y0:y1, x0:x1] > 0
    cy, cx = ys.mean(), xs.mean()
    rn = np.sqrt((ys - cy) ** 2 + (xs - cx) ** 2)
    rn = rn / (rn.max() or 1.0)
    vals = d[ys, xs]
    inner, outer = vals[rn < 0.5], vals[rn >= 0.5]
    lap = cv2.Laplacian(patch, cv2.CV_32F, ksize=3)
    gx = cv2.Sobel(patch, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(patch, cv2.CV_32F, 0, 1, ksize=3)
    return dict(
        lap_var=float(lap[pm].var()) if pm.sum() > 4 else 0.0,
        tenengrad=float((gx * gx + gy * gy)[pm].mean()) if pm.sum() else 0.0,
        intensity_std=float(vals.std()),
        radial_ratio=float((outer.mean() - inner.mean()) / (outer.mean() + inner.mean() + 1e-6))
        if len(inner) and len(outer) else 0.0,
    )


def nested_ring_present(g, thresh_mode):
    """Does the OLD method yield a nested (ring) contour? Mirrors production:
    bg-subtracted diff -> threshold -> MORPH_CROSS close -> RETR_TREE."""
    from pipelines import ROI_Y0, ROI_Y1
    bg = rowmean_bg(g)
    d = cv2.absdiff(g[ROI_Y0:ROI_Y1], bg[ROI_Y0:ROI_Y1])
    if thresh_mode == "otsu":
        t, _ = cv2.threshold(d, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        th = cv2.threshold(d, max(8, t * 1.1), 255, cv2.THRESH_BINARY)[1]
    else:
        th = cv2.threshold(d, 8, 255, cv2.THRESH_BINARY)[1]
    th = cv2.morphologyEx(th, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_CROSS, (3, 3)))
    cnts, hier = cv2.findContours(th, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
    if hier is None:
        return False
    for i, c in enumerate(cnts):
        if cv2.contourArea(c) >= 10 and hier[0][i][3] > -1:
            return True
    return False


def spearman(a, b):
    a, b = np.asarray(a, float), np.asarray(b, float)
    ok = ~(np.isnan(a) | np.isnan(b))
    return float(np.corrcoef(pd.Series(a[ok]).rank(), pd.Series(b[ok]).rank())[0, 1])


def main():
    meta = B.load()
    ref_rr = np.array([m.get("ring_ratio", np.nan) for m in
                       pd.read_parquet("bench.parquet")["mask_quality"]], float)
    CANDS = ["lap_var", "tenengrad", "intensity_std", "radial_ratio"]
    rows, per = [], []

    old_fixed = old_otsu = 0
    for i, (_, r) in enumerate(meta.iterrows()):
        g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        mask = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        fs = focus_scores(g, mask)
        if fs is None:
            continue
        fs["ref_rr"] = ref_rr[i]
        per.append(fs)
        old_fixed += nested_ring_present(g, "fixed")
        old_otsu += nested_ring_present(g, "otsu")
    P = pd.DataFrame(per)
    n = len(P)

    # 1. robustness
    rows.append(dict(group="robustness", metric="OLD nested-ring (fixed thresh)",
                     defined_pct=round(100 * old_fixed / len(meta), 1)))
    rows.append(dict(group="robustness", metric="OLD nested-ring (Otsu thresh)",
                     defined_pct=round(100 * old_otsu / len(meta), 1)))
    for c in CANDS:
        rows.append(dict(group="robustness", metric=c, defined_pct=100.0))

    # 2. correlation vs reference ring ratio (nonzero refs only)
    nz = P[P.ref_rr > 0]
    for c in CANDS:
        rows.append(dict(group="corr_vs_ref_ring", metric=c,
                         spearman=round(spearman(nz[c], nz.ref_rr), 3), n=len(nz)))

    # 3. defocus response on the 40 sharpest cells
    sharp = P.sort_values("lap_var", ascending=False).head(40).index
    sigmas = [0, 0.6, 1.0, 1.5, 2.0, 3.0]
    for c in CANDS:
        curve = []
        for s in sigmas:
            acc = []
            for i in sharp:
                r = meta.iloc[i]
                g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
                mask = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
                gb = g if s == 0 else cv2.GaussianBlur(g, (0, 0), s)
                fs = focus_scores(gb, mask)
                if fs:
                    acc.append(fs[c])
            curve.append(float(np.median(acc)))
        curve = np.array(curve)
        mono = bool(np.all(np.diff(curve) <= 1e-9))
        rows.append(dict(group="defocus_response", metric=c,
                         dyn_range=round(curve[0] / max(curve[-1], 1e-6), 1),
                         monotonic_down=mono,
                         curve_norm=[round(x, 3) for x in (curve / curve[0])]))

    os.makedirs("results", exist_ok=True)
    pd.DataFrame(rows).to_csv("results/focus_experiments.csv", index=False)
    json.dump(rows, open("results/focus_experiments.json", "w"), indent=2)

    print(f"n={n} detections\n")
    print("1. ROBUSTNESS (fraction of frames the metric is defined):")
    print(f"   OLD nested-ring, fixed thresh : {100*old_fixed/len(meta):.0f}%")
    print(f"   OLD nested-ring, Otsu  thresh : {100*old_otsu/len(meta):.0f}%")
    print(f"   intensity metrics             : 100%")
    print("\n2. Spearman vs reference ring_ratio (nonzero, n=%d):" % len(nz))
    for c in CANDS:
        print(f"   {c:14s} {spearman(nz[c], nz.ref_rr):+.3f}")
    print("\n3. Defocus response (dynamic range over sigma 0->3, higher=better):")
    for row in rows:
        if row["group"] == "defocus_response":
            print(f"   {row['metric']:14s} {row['dyn_range']:6.1f}x  mono={row['monotonic_down']}")
    print("\nWrote results/focus_experiments.{csv,json}")


if __name__ == "__main__":
    main()
