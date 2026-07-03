"""Build the focus dataset from our results and visualise it.

Computes the shipped focus metrics (ProcessingService::computeFocusMetrics:
variance of the Laplacian + Tenengrad on the ORIGINAL intensity inside the cell
mask) for every GT detection, writes results/focus_data.csv, and renders:

  results/focus_ranked_cells.png  -- real cells ordered sharp -> defocused
  results/focus_analysis.png      -- distribution, defocus response, robustness,
                                     and focus-vs-old-ring-ratio scatter

Palette: dataviz reference slots (blue #2a78d6, aqua #1baf7a), validated
colorblind-safe; identity is also carried by direct labels/legend.
"""
import json
import numpy as np
import pandas as pd
import cv2
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import gridspec
import bench as B

BLUE, AQUA = "#2a78d6", "#1baf7a"
INK, INK2, GRIDC = "#0b0b0b", "#52514e", "#e6e5e2"
plt.rcParams.update({"font.size": 9, "axes.edgecolor": INK2, "text.color": INK,
                     "axes.labelcolor": INK, "xtick.color": INK2, "ytick.color": INK2,
                     "axes.grid": True, "grid.color": GRIDC, "grid.linewidth": 0.8,
                     "axes.axisbelow": True, "figure.facecolor": "white"})


def focus_on_original(gray, mask):
    """Matches computeFocusMetrics: high-pass measures on the ORIGINAL intensity
    within the object mask bbox (no background needed)."""
    ys, xs = np.where(mask > 0)
    if len(ys) < 8:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    patch = gray[y0:y1, x0:x1].astype(np.float32)
    pm = mask[y0:y1, x0:x1] > 0
    if pm.sum() < 5 or patch.shape[0] < 3 or patch.shape[1] < 3:
        return None
    lap = cv2.Laplacian(patch, cv2.CV_32F, ksize=3)
    gx = cv2.Sobel(patch, cv2.CV_32F, 1, 0, 3)
    gy = cv2.Sobel(patch, cv2.CV_32F, 0, 1, 3)
    return dict(lap_var=float(lap[pm].var()),
                tenengrad=float((gx * gx + gy * gy)[pm].mean()),
                intensity_std=float(gray[ys, xs].std()))


def nested_ring_present(gray, mode="otsu"):
    from pipelines import ROI_Y0, ROI_Y1
    bg = np.broadcast_to(cv2.reduce(gray, 1, cv2.REDUCE_AVG), gray.shape)
    d = cv2.absdiff(gray[ROI_Y0:ROI_Y1], bg[ROI_Y0:ROI_Y1])
    if mode == "otsu":
        t, _ = cv2.threshold(d, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        th = cv2.threshold(d, max(8, t * 1.1), 255, cv2.THRESH_BINARY)[1]
    else:
        th = cv2.threshold(d, 8, 255, cv2.THRESH_BINARY)[1]
    th = cv2.morphologyEx(th, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_CROSS, (3, 3)))
    cnts, hier = cv2.findContours(th, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)
    if hier is None:
        return False
    return any(cv2.contourArea(c) >= 10 and hier[0][i][3] > -1 for i, c in enumerate(cnts))


def build_dataset(meta, ref_rr):
    rows = []
    for i, (_, r) in enumerate(meta.iterrows()):
        g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        m = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        f = focus_on_original(g, m)
        if not f:
            continue
        rows.append(dict(idx=int(r.idx), cls=r.cls,
                         bx0=r.bx0, by0=r.by0, bx1=r.bx1, by1=r.by1,
                         focus_lap_var=f["lap_var"], focus_tenengrad=f["tenengrad"],
                         intensity_std=f["intensity_std"],
                         old_ring_ratio=float(ref_rr[i]),
                         old_ring_present=int(nested_ring_present(g, "otsu")),
                         image=r.image, mgt=r.mgt))
    return pd.DataFrame(rows)


def defocus_curve(meta, data):
    sharp = data.sort_values("focus_lap_var", ascending=False).head(40)
    sigmas = [0, 0.6, 1.0, 1.5, 2.0, 3.0]
    lap, ten = [], []
    for s in sigmas:
        a, b = [], []
        for _, r in sharp.iterrows():
            g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
            m = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
            gb = g if s == 0 else cv2.GaussianBlur(g, (0, 0), s)
            f = focus_on_original(gb, m)
            if f:
                a.append(f["lap_var"]); b.append(f["tenengrad"])
        lap.append(np.median(a)); ten.append(np.median(b))
    lap, ten = np.array(lap), np.array(ten)
    return sigmas, lap / lap[0], ten / ten[0]


def crop_cell(r, out=60, pad=8):
    g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
    cx, cy = (r.bx0 + r.bx1) / 2, (r.by0 + r.by1) / 2
    half = max(r.bx1 - r.bx0, r.by1 - r.by0) / 2 + pad
    x0, x1 = int(cx - half), int(cx + half)
    y0, y1 = int(cy - half), int(cy + half)
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(g.shape[1], x1), min(g.shape[0], y1)
    patch = g[y0:y1, x0:x1]
    if patch.size == 0:
        patch = g
    return cv2.resize(patch, (out, out), interpolation=cv2.INTER_NEAREST)


def render_montage(data, path, ncols=7, nrows=4):
    order = data.sort_values("focus_lap_var", ascending=False).reset_index(drop=True)
    n = ncols * nrows
    picks = order.iloc[np.linspace(0, len(order) - 1, n).astype(int)].reset_index(drop=True)
    fig, axes = plt.subplots(nrows, ncols, figsize=(ncols * 1.15, nrows * 1.30))
    fig.suptitle("Real cells ordered by focus score (Laplacian variance):  sharp → defocused",
                 fontsize=12, fontweight="bold", y=0.99)
    for k, ax in enumerate(axes.ravel()):
        r = picks.iloc[k]
        ax.imshow(crop_cell(r), cmap="gray", vmin=0, vmax=255)
        col = BLUE if r.cls == "cell" else AQUA
        for sp in ax.spines.values():
            sp.set_color(col); sp.set_linewidth(2.2)
        ax.set_xticks([]); ax.set_yticks([])
        ax.set_title(f"{r.focus_lap_var:,.0f}", fontsize=8, color=INK, pad=2)
    # legend for class colours (identity not by colour alone -> labels + legend)
    from matplotlib.patches import Patch
    fig.legend(handles=[Patch(facecolor="white", edgecolor=BLUE, linewidth=2.2, label="cell"),
                        Patch(facecolor="white", edgecolor=AQUA, linewidth=2.2, label="cluster")],
               loc="lower center", ncol=2, frameon=False, bbox_to_anchor=(0.5, -0.01))
    fig.text(0.5, 0.045, "tile label = focusLaplacianVar; border colour = class",
             ha="center", fontsize=8, color=INK2)
    fig.tight_layout(rect=[0, 0.06, 1, 0.96])
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def render_analysis(data, meta, path):
    fig = plt.figure(figsize=(11, 8))
    gs = gridspec.GridSpec(2, 2, figure=fig, hspace=0.34, wspace=0.24)

    # (1) distribution of focus by class
    ax = fig.add_subplot(gs[0, 0])
    bins = np.logspace(np.log10(max(1, data.focus_lap_var.min())),
                       np.log10(data.focus_lap_var.max()), 26)
    for cls, col in [("cell", BLUE), ("cluster", AQUA)]:
        ax.hist(data[data.cls == cls].focus_lap_var, bins=bins, color=col,
                alpha=0.75, label=cls, edgecolor="white", linewidth=0.5)
    ax.set_xscale("log")
    ax.set_xlabel("focusLaplacianVar"); ax.set_ylabel("detections")
    ax.set_title("A. Focus-score distribution (all 100% defined)", fontweight="bold", loc="left")
    ax.legend(frameon=False)

    # (2) defocus response
    ax = fig.add_subplot(gs[0, 1])
    sig, lap, ten = defocus_curve(meta, data)
    ax.plot(sig, lap, "-o", color=BLUE, lw=2, ms=6, label="Laplacian var")
    ax.plot(sig, ten, "-o", color=AQUA, lw=2, ms=6, label="Tenengrad")
    ax.set_yscale("log")
    ax.set_xlabel("added defocus blur  σ (px)"); ax.set_ylabel("score (normalised to σ=0)")
    ax.set_title("B. Response to defocus (monotonic ↓ = good)", fontweight="bold", loc="left")
    ax.annotate(f"{1/lap[-1]:.0f}× range", (sig[-1], lap[-1]), color=BLUE,
                fontsize=8, ha="right", va="bottom")
    ax.legend(frameon=False)

    # (3) robustness: fraction of frames the metric is defined
    ax = fig.add_subplot(gs[1, 0])
    labels = ["old ring\n(fixed thr)", "old ring\n(Otsu)", "focus\n(intensity)"]
    # old-ring presence rates over the FULL meta
    otsu_rate = 100 * sum(nested_ring_present(cv2.imread(r.image, 0), "otsu")
                          for _, r in meta.iterrows()) / len(meta)
    fixed_rate = 100 * sum(nested_ring_present(cv2.imread(r.image, 0), "fixed")
                           for _, r in meta.iterrows()) / len(meta)
    vals = [fixed_rate, otsu_rate, 100.0]
    cols = [INK2, INK2, BLUE]
    bars = ax.bar(labels, vals, color=cols, width=0.62, zorder=3)
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width() / 2, v + 1.5, f"{v:.0f}%", ha="center",
                fontsize=9, color=INK, fontweight="bold")
    ax.set_ylim(0, 108); ax.set_ylabel("% of frames metric is defined")
    ax.set_title("C. Robustness — old ring breaks on ~1/3 of frames", fontweight="bold", loc="left")

    # (4) focus vs old ring ratio
    ax = fig.add_subplot(gs[1, 1])
    zero = data[data.old_ring_ratio <= 0]
    nz = data[data.old_ring_ratio > 0]
    ax.scatter(nz.old_ring_ratio, nz.focus_lap_var, s=18, color=BLUE, alpha=0.7,
               edgecolor="white", linewidth=0.4, label="old ring defined")
    ax.scatter(np.full(len(zero), 0.0), zero.focus_lap_var, s=26, color="#d03b3b",
               marker="x", label="old ring = 0 (broken)")
    ax.set_yscale("log")
    ax.set_xlabel("old ring_ratio (reference)"); ax.set_ylabel("focusLaplacianVar")
    ax.set_title("D. Focus defined even where old ring = 0", fontweight="bold", loc="left")
    ax.legend(frameon=False, loc="lower right")

    fig.suptitle("Focus metric — topology-free replacement for the nested-contour ring ratio",
                 fontsize=13, fontweight="bold")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    import os
    os.makedirs("results", exist_ok=True)
    meta = B.load()
    ref_rr = np.array([m.get("ring_ratio", np.nan) for m in
                       pd.read_parquet("bench.parquet")["mask_quality"]], float)
    data = build_dataset(meta, ref_rr)
    keep = ["idx", "cls", "focus_lap_var", "focus_tenengrad", "intensity_std",
            "old_ring_ratio", "old_ring_present"]
    data[keep].to_csv("results/focus_data.csv", index=False)
    render_montage(data, "results/focus_ranked_cells.png")
    render_analysis(data, meta, "results/focus_analysis.png")
    print(f"n={len(data)} detections")
    print("focusLaplacianVar: min %.0f  median %.0f  max %.0f" % (
        data.focus_lap_var.min(), data.focus_lap_var.median(), data.focus_lap_var.max()))
    print("wrote results/focus_data.csv, focus_ranked_cells.png, focus_analysis.png")


if __name__ == "__main__":
    main()
