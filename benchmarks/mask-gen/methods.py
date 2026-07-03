"""Prototype implementations of the 'worth trying next' methods, plus a
uniform scorer so every method is measured against the same GT.

Grouped: (1) background models, (2) hysteresis threshold, (3) watershed,
(4) shape regularization, (5) throughput paths. Method (6), a distilled CNN,
needs training infrastructure and is out of scope for a same-session prototype.
"""
import time
import cv2
import numpy as np
import pipelines as P
import bench as B

BB = lambda r: (r.bx0, r.by0, r.bx1, r.by1)


# ===========================================================================
# Uniform scorer
# ===========================================================================
def score(meta, pipe, cfg=None, bgfn=None, framefn=None, fill=True):
    """Mean IoU / detection@0.5 over all detections (bbox-cropped)."""
    cfg = cfg or {}
    bgfn = bgfn or P.estimate_background
    ious = []
    for _, r in meta.iterrows():
        g0 = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        gt = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        g = framefn(g0) if framefn else g0
        bg = bgfn(g0)                      # background modelled from the clean frame
        m = pipe(g, bg, cfg)
        if fill:
            m = P.fill_contours(m)
        pc = B.crop_bbox(m, BB(r), 6, g.shape)
        gc = B.crop_bbox(gt, BB(r), 6, g.shape)
        iou, *_ = B.iou_dice(pc, gc)
        ious.append((iou, r.cls))
    a = np.array([x[0] for x in ious])
    cl = np.array([x[1] for x in ious])
    nanmean = lambda v: float(v.mean()) if len(v) else float("nan")
    return dict(iou=float(a.mean()), det=float((a > 0.5).mean()),
                iou_cell=nanmean(a[cl == "cell"]),
                iou_cluster=nanmean(a[cl == "cluster"]))


def latency_us(meta, pipe, cfg=None, n=40, reps=20):
    cfg = cfg or {}
    sample = meta.drop_duplicates("image").head(n)
    prep = [(cv2.imread(r.image, cv2.IMREAD_GRAYSCALE),) for _, r in sample.iterrows()]
    prep = [(g, P.estimate_background(g)) for (g,) in prep]
    for g, bg in prep:
        P.fill_contours(pipe(g, bg, cfg))
    ts = []
    for _ in range(reps):
        for g, bg in prep:
            t0 = time.perf_counter()
            P.fill_contours(pipe(g, bg, cfg))
            ts.append((time.perf_counter() - t0) * 1e6)
    return float(np.median(ts))


# ===========================================================================
# (1) Background models -- single-frame estimators (substitute for row-median)
# ===========================================================================
def bg_row_median(g):
    return np.broadcast_to(np.median(g, axis=1, keepdims=True).astype(np.uint8), g.shape).copy()


def bg_row_mean(g):
    return np.broadcast_to(g.mean(axis=1, keepdims=True).astype(np.uint8), g.shape).copy()


def bg_morph_open(g, k=25):
    """Grayscale opening = classic single-frame background (rolling-ball-like)."""
    ker = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (k, k))
    return cv2.morphologyEx(g, cv2.MORPH_OPEN, ker)


def bg_gaussian(g, k=51):
    return cv2.GaussianBlur(g, (k, k), 0)


def bg_poly(g, order=2):
    """Low-order polynomial surface fit -- smooth illumination model."""
    h, w = g.shape
    ys, xs = np.mgrid[0:h, 0:w]
    yv = (ys.ravel() / h).astype(np.float32)
    xv = (xs.ravel() / w).astype(np.float32)
    cols = [np.ones_like(xv)]
    for i in range(1, order + 1):
        cols += [xv ** i, yv ** i]
    A = np.stack(cols, 1)
    coef, *_ = np.linalg.lstsq(A, g.ravel().astype(np.float32), rcond=None)
    return np.clip((A @ coef).reshape(h, w), 0, 255).astype(np.uint8)


# ---- temporal background (MOG2 / KNN) on a synthesized per-strip sequence ----
def temporal_eval(meta, kind="MOG2", hist=8, noise=3.0, framefn=None, drift_history=False):
    """Fairness note: this GT has no video, so per strip we synthesize `hist`
    empty frames (true field = row-median bg + Gaussian noise) followed by the
    real frame, and read the subtractor's foreground on that last frame. This
    tests the *mechanism* with clean history, not real drift.

    framefn applies a transform to the test frame; drift_history=True applies it
    to the history frames too, simulating a subtractor that has *continuously
    adapted* to the drift (the real selling point of temporal background)."""
    rng = np.random.RandomState(0)
    y0, y1 = P.ROI_Y0, P.ROI_Y1
    ious = []
    for _, r in meta.iterrows():
        g0 = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        gt = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        g = framefn(g0) if framefn else g0
        # empty field for history; apply the SAME transform to the 2D field so
        # its spatial structure (e.g. a shading ramp) survives into the history.
        field = bg_row_median(g0)
        if framefn and drift_history:
            field = framefn(field)
        base = field[y0:y1].astype(np.float32)
        if kind == "MOG2":
            sub = cv2.createBackgroundSubtractorMOG2(history=hist, varThreshold=16, detectShadows=False)
        else:
            sub = cv2.createBackgroundSubtractorKNN(history=hist, dist2Threshold=400, detectShadows=False)
        for _ in range(hist):
            hf = np.clip(base + rng.randn(*base.shape) * noise, 0, 255).astype(np.uint8)
            sub.apply(hf, learningRate=-1)
        fg = sub.apply(g[y0:y1], learningRate=0)
        fg = (fg > 0).astype(np.uint8) * 255
        ker = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
        fg = cv2.morphologyEx(fg, cv2.MORPH_CLOSE, ker)
        fg = cv2.morphologyEx(fg, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3)))
        m = np.zeros_like(g); m[y0:y1] = fg
        m = P.fill_contours(m)
        pc = B.crop_bbox(m, BB(r), 6, g.shape); gc = B.crop_bbox(gt, BB(r), 6, g.shape)
        iou, *_ = B.iou_dice(pc, gc); ious.append(iou)
    a = np.array(ious)
    return dict(iou=float(a.mean()), det=float((a > 0.5).mean()))


def ewma_temporal_eval(meta, alpha=0.05, hist=8, noise=3.0, framefn=None, drift_history=False):
    """Cheap temporal background: a running average (EWMA) updated over the
    synthesized history, then the OPT pipeline with that background. This is the
    budget-friendly stand-in for MOG2 -- update cost is one weighted add (~few
    us) instead of a per-pixel GMM."""
    rng = np.random.RandomState(0)
    ious = []
    for _, r in meta.iterrows():
        g0 = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        gt = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        g = framefn(g0) if framefn else g0
        field = bg_row_median(g0)
        if framefn and drift_history:
            field = framefn(field)
        acc = field.astype(np.float32)
        for _ in range(hist):
            hf = np.clip(field.astype(np.float32) + rng.randn(*field.shape) * noise, 0, 255)
            acc = (1 - alpha) * acc + alpha * hf
        bg = acc.astype(np.uint8)
        m = P.fill_contours(P.proposed_pipeline(g, bg, P.PROPOSED_OPT_CFG))
        pc = B.crop_bbox(m, BB(r), 6, g.shape); gc = B.crop_bbox(gt, BB(r), 6, g.shape)
        iou, *_ = B.iou_dice(pc, gc); ious.append(iou)
    a = np.array(ious)
    return dict(iou=float(a.mean()), det=float((a > 0.5).mean()))


# ===========================================================================
# core enhanced image (absdiff [+ optional top-hat]) on the ROI band
# ===========================================================================
def _core_enhance(gray, bg, cfg):
    y0, y1 = P._roi_slice(gray, cfg)
    d = cv2.absdiff(gray[y0:y1], bg[y0:y1])
    if cfg.get("enhance", "tophat") == "tophat":
        tk = P._odd(cfg.get("tophat_kernel", 21))
        shp = cv2.MORPH_RECT if cfg.get("tophat_shape", "rect") == "rect" else cv2.MORPH_ELLIPSE
        d = cv2.morphologyEx(d, cv2.MORPH_TOPHAT, cv2.getStructuringElement(shp, (tk, tk)))
    return d, (y0, y1)


def _close_open(binary, close_k=5, open_k=3):
    r = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (close_k, close_k)))
    if open_k:
        r = cv2.morphologyEx(r, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (open_k, open_k)))
    return r


# ===========================================================================
# (2) Hysteresis (double) threshold
# ===========================================================================
def hysteresis_pipeline(gray, bg, cfg):
    e, (y0, y1) = _core_enhance(gray, bg, cfg)
    otsu, _ = cv2.threshold(e, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    hi = otsu * cfg.get("hi_scale", 1.0)
    lo = otsu * cfg.get("lo_scale", 0.5)
    strong = (e >= hi).astype(np.uint8)
    weak = (e >= lo).astype(np.uint8)
    n, lbl = cv2.connectedComponents(weak, 8)
    keep = np.zeros(n, bool)
    keep[np.unique(lbl[strong > 0])] = True
    keep[0] = False
    r = keep[lbl].astype(np.uint8) * 255
    r = _close_open(r, cfg.get("morph_kernel_size", 5), cfg.get("open_iterations", 1) and 3)
    m = np.zeros_like(gray); m[y0:y1] = r
    return m


# ===========================================================================
# (3) Distance-transform watershed (instance separation)
# ===========================================================================
def watershed_pipeline(gray, bg, cfg):
    m0 = P.proposed_pipeline(gray, bg, dict(P.PROPOSED_OPT_CFG, **cfg))
    y0, y1 = P._roi_slice(gray, cfg)
    band = m0[y0:y1]
    if band.max() == 0:
        return m0
    dist = cv2.distanceTransform(band, cv2.DIST_L2, 3)
    _, peaks = cv2.threshold(dist, cfg.get("peak_frac", 0.5) * dist.max(), 255, 0)
    peaks = peaks.astype(np.uint8)
    nmark, markers = cv2.connectedComponents(peaks, 8)
    unknown = cv2.subtract(band, peaks)
    markers = markers + 1
    markers[unknown > 0] = 0
    bgr = cv2.cvtColor(band, cv2.COLOR_GRAY2BGR)
    markers = cv2.watershed(bgr, markers)
    out = np.zeros_like(band)
    out[markers > 1] = 255                       # split regions (boundaries = -1)
    m = np.zeros_like(gray); m[y0:y1] = out
    return m


# ===========================================================================
# (4) Shape regularization (ellipse / convex hull fit)
# ===========================================================================
def shape_pipeline(gray, bg, cfg):
    m0 = P.proposed_pipeline(gray, bg, dict(P.PROPOSED_OPT_CFG, **cfg))
    mode = cfg.get("shape", "ellipse")
    cnts, _ = cv2.findContours(m0, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    out = np.zeros_like(m0)
    for c in cnts:
        if cv2.contourArea(c) < 10:
            continue
        if mode == "hull":
            cv2.drawContours(out, [cv2.convexHull(c)], -1, 255, cv2.FILLED)
        elif mode == "ellipse" and len(c) >= 5:
            cv2.ellipse(out, cv2.fitEllipse(c), 255, cv2.FILLED)
        else:
            cv2.drawContours(out, [c], -1, 255, cv2.FILLED)
    return out
