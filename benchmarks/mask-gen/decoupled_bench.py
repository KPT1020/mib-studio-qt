"""Quantify the decoupled size-measurement safeguard on the GT dataset.

Mirrors the C++ change in ProcessingService: detection uses the per-frame Otsu
cut (applyProcessingThreshold), but area is re-measured on the fixed-threshold
"measurement mask" via the object-centroid contour match (matchContourContaining),
falling back to the detection contour when no counterpart exists.

For every GT detection we compute the cell's convex-hull area three ways and
report, across the population:
  * detection recall     -- fixed vs adaptive (a co-located object exists)
  * measurement fallback -- adaptive object with NO fixed counterpart (area then
                            cannot be stabilised and stays on the adaptive cut)
  * area drift PREVENTED  -- |adaptive-raw area - fixed basis| / fixed basis
                            (what downstream analysis would have seen)
  * decoupled fidelity    -- |decoupled area - fixed basis| / fixed basis
                            (should be ~0: measurement stayed on the fixed basis)

Run after download_gt.py (needs meta.pkl + cache/).
"""
import numpy as np
import pandas as pd
import cv2
import pipelines as P

# C++ ProcessingConfig defaults for the adaptive path.
CFG = dict(gaussian_blur_size=3, morph_kernel_size=3, morph_iterations=1,
           bg_subtract_threshold=8, otsu_scale=1.1)
MIN_NOISE_AREA = 10.0  # matches ProcessingService::findContours


def _diff_and_kernel(gray, bg, cfg):
    """The background-subtracted ROI diff + morphology kernel, exactly as
    current_pipeline builds them (so fixed and adaptive share one diff)."""
    blurK = P._odd(cfg.get("gaussian_blur_size", 3))
    morphK = P._odd(cfg.get("morph_kernel_size", 3))
    morphIter = max(1, cfg.get("morph_iterations", 1))
    y0, y1 = P._roi_slice(gray, cfg)
    cur = cv2.GaussianBlur(gray[y0:y1], (blurK, blurK), 0)
    bgb = cv2.GaussianBlur(bg[y0:y1], (blurK, blurK), 0)
    diff = cv2.subtract(cur, bgb)
    kernel = cv2.getStructuringElement(cv2.MORPH_CROSS, (morphK, morphK))
    return diff, kernel, morphIter, (y0, y1), gray.shape


def _morph_full(binary, kernel, morphIter, band, shape):
    r = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, kernel, iterations=morphIter)
    r = cv2.morphologyEx(r, cv2.MORPH_OPEN, kernel, iterations=morphIter)
    m = np.zeros(shape, np.uint8)
    m[band[0]:band[1]] = r
    return m


def fixed_mask(gray, bg, cfg):
    diff, kernel, mi, band, shape = _diff_and_kernel(gray, bg, cfg)
    t = max(0, cfg.get("bg_subtract_threshold", 8))
    _, th = cv2.threshold(diff, t, 255, cv2.THRESH_BINARY)
    return _morph_full(th, kernel, mi, band, shape)


def adaptive_mask(gray, bg, cfg):
    """Faithful port of ProcessingService::applyProcessingThreshold (adaptive)."""
    diff, kernel, mi, band, shape = _diff_and_kernel(gray, bg, cfg)
    fixedT = max(0, cfg.get("bg_subtract_threshold", 8))
    otsu_t, _ = cv2.threshold(diff, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    t = max(float(fixedT), otsu_t * cfg.get("otsu_scale", 1.1))
    if t <= float(fixedT):
        _, th = cv2.threshold(diff, fixedT, 255, cv2.THRESH_BINARY)
    else:
        _, th = cv2.threshold(diff, t, 255, cv2.THRESH_BINARY)
    return _morph_full(th, kernel, mi, band, shape)


def _contours(mask):
    cs, _ = cv2.findContours(mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
    return [c for c in cs if cv2.contourArea(c) >= MIN_NOISE_AREA]


def _hull_area(c):
    return cv2.contourArea(cv2.convexHull(c))


def _filled(c, shape):
    m = np.zeros(shape, np.uint8)
    cv2.drawContours(m, [c], -1, 255, cv2.FILLED)
    return m > 0


def _match_overlap(cands, obj, shape, min_iom=0.2):
    """Candidate with the largest region overlap with the detected object,
    scored as intersection-over-min-area; None if the best is below min_iom.
    Mirrors ProcessingService::matchContourByOverlap. Robust to the fragmented
    / offset fixed masks that defeat a strict centroid-containment test."""
    A = _filled(obj, shape)
    aa = int(A.sum())
    if aa == 0:
        return None
    best, best_iom = None, 0.0
    for c in cands:
        B = _filled(c, shape)
        inter = int(np.logical_and(A, B).sum())
        if inter == 0:
            continue
        iom = inter / min(aa, int(B.sum()))
        if iom > best_iom:
            best, best_iom = c, iom
    return best if best_iom >= min_iom else None


def _object_for_bbox(contours, bb):
    """The detected object for a GT bbox: the contour whose filled region most
    overlaps the bbox (0 area = not detected)."""
    x0, y0, x1, y1 = int(bb[0]), int(bb[1]), int(bb[2]), int(bb[3])
    best, best_ov = None, 0
    for c in contours:
        bx, by, bw, bh = cv2.boundingRect(c)
        ix = max(0, min(x1, bx + bw) - max(x0, bx))
        iy = max(0, min(y1, by + bh) - max(y0, by))
        ov = ix * iy
        if ov > best_ov:
            best, best_ov = c, ov
    return best


def main():
    meta = pd.read_pickle("meta.pkl")
    rows = []
    for _, r in meta.iterrows():
        gray = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        if gray is None:
            continue
        bg = P.estimate_background(gray)
        bb = (r.bx0, r.by0, r.bx1, r.by1)

        fc = _contours(fixed_mask(gray, bg, CFG))
        ac = _contours(adaptive_mask(gray, bg, CFG))
        fobj = _object_for_bbox(fc, bb)
        aobj = _object_for_bbox(ac, bb)

        rec = dict(idx=r.idx, cls=r.cls,
                   det_fixed=int(fobj is not None),
                   det_adaptive=int(aobj is not None))
        if aobj is not None:
            a_raw = _hull_area(aobj)                        # no decoupling
            mobj = _match_overlap(fc, aobj, gray.shape)     # measurement match
            fell_back = mobj is None
            a_dec = _hull_area(mobj) if mobj is not None else a_raw
            a_fixed = _hull_area(fobj) if fobj is not None else np.nan
            rec.update(area_adaptive_raw=a_raw, area_decoupled=a_dec,
                       area_fixed=a_fixed, fallback=int(fell_back))
        rows.append(rec)

    df = pd.DataFrame(rows)
    df.to_csv("results/decoupled_area.csv", index=False)

    n = len(df)
    print(f"\n== Decoupled size measurement on GT set (n={n}, cfg={CFG}) ==\n")
    print(f"detection recall (co-located object exists):")
    print(f"   fixed threshold    {df.det_fixed.mean()*100:5.1f}%")
    print(f"   adaptive (Otsu)    {df.det_adaptive.mean()*100:5.1f}%"
          f"   (+{(df.det_adaptive.mean()-df.det_fixed.mean())*100:.1f} pts)\n")

    det = df[df.det_adaptive == 1].copy()
    print(f"among {len(det)} adaptive detections:")
    print(f"   measurement fallback (no fixed counterpart)  {det.fallback.mean()*100:5.1f}%")

    # Area comparisons only where the fixed basis is defined.
    ok = det[det.area_fixed.notna() & (det.area_fixed > 0)].copy()
    ok["drift_raw"] = (ok.area_adaptive_raw - ok.area_fixed).abs() / ok.area_fixed
    ok["drift_dec"] = (ok.area_decoupled - ok.area_fixed).abs() / ok.area_fixed
    bias_raw = (ok.area_adaptive_raw - ok.area_fixed) / ok.area_fixed
    print(f"\narea vs fixed basis (n={len(ok)} with a fixed counterpart):")
    print(f"   WITHOUT decoupling (adaptive-raw):")
    print(f"       median |drift|   {ok.drift_raw.median()*100:5.1f}%   "
          f"mean {ok.drift_raw.mean()*100:5.1f}%   p90 {ok.drift_raw.quantile(.9)*100:5.1f}%")
    print(f"       signed bias      {bias_raw.median()*100:+5.1f}% (median)  "
          f"{bias_raw.mean()*100:+5.1f}% (mean)")
    print(f"   WITH decoupling (this change):")
    print(f"       median |drift|   {ok.drift_dec.median()*100:5.1f}%   "
          f"mean {ok.drift_dec.mean()*100:5.1f}%   p90 {ok.drift_dec.quantile(.9)*100:5.1f}%")
    within = (ok.drift_dec <= 0.02).mean() * 100
    print(f"       within 2% of fixed basis: {within:.1f}% of objects")
    print("\nwrote results/decoupled_area.csv")


if __name__ == "__main__":
    main()
