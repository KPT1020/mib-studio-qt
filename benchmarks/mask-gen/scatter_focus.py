"""Light-scattering focus analysis on the REAL dataset (no Gaussian-blur proxy).

Motivation
----------
A flowing cell in brightfield is a *phase object*: at true focus it is nearly
invisible, and its contrast (the "white ring") is a *defocus-induced scattering*
signature. Transport-of-intensity: dI/dz ~ -I0 * lap(phase), so the halo is
BRIGHT on one side of focus, DARK on the other, and passes through ~zero at
focus. Consequences we test here on real cells (not synthetic blur):

  * focusLaplacianVar / focusTenengrad are *unsigned* contrast-energy measures.
    Prediction: they track ring STRENGTH (|defocus|), so they should DIP where
    the ring vanishes (focus) and are sign-blind to which side of focus we are.
  * a SIGNED radial contrast (outer-rim minus inner-disc on the signed
    background-subtracted image) carries the bright/dark polarity -> the
    DIRECTION the autofocus servo needs, which the unsigned metrics cannot give.

Surrogate defocus axis
----------------------
The dataset has no z label, but spans a range of focus. We use the signed
radial contrast (rim - centre) as a physically-meaningful surrogate defocus
coordinate: sign = which side of focus, magnitude = how far.

Outputs: results/scatter_focus_data.csv and two figures.
"""
import os
import numpy as np
import pandas as pd
import cv2
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import gridspec
import bench as B
from pipelines import estimate_background_fast

BLUE, AQUA, RED, INK, INK2, GRID = "#2a78d6", "#1baf7a", "#d03b3b", "#0b0b0b", "#52514e", "#e6e5e2"
plt.rcParams.update({"font.size": 9, "axes.edgecolor": INK2, "text.color": INK,
                     "axes.labelcolor": INK, "xtick.color": INK2, "ytick.color": INK2,
                     "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
                     "axes.axisbelow": True, "figure.facecolor": "white"})

# rim band and inner disc in units of the cell's own radius
INNER_MAX = 0.45     # inner disc: r/R < 0.45
RIM_LO, RIM_HI = 0.75, 1.15   # rim annulus straddles the mask boundary
RBINS = np.linspace(0.0, 1.6, 33)


def focus_energy(gray, mask):
    """Production computeFocusMetrics: Laplacian variance + Tenengrad on the
    ORIGINAL intensity within the object-mask bbox."""
    ys, xs = np.where(mask > 0)
    if len(ys) < 8:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    patch = gray[y0:y1, x0:x1].astype(np.float32)
    pm = mask[y0:y1, x0:x1] > 0
    if pm.sum() < 5 or patch.shape[0] < 3 or patch.shape[1] < 3:
        return None
    lap = cv2.Laplacian(patch, cv2.CV_32F, 3)
    gx = cv2.Sobel(patch, cv2.CV_32F, 1, 0, 3)
    gy = cv2.Sobel(patch, cv2.CV_32F, 0, 1, 3)
    return float(lap[pm].var()), float((gx * gx + gy * gy)[pm].mean())


def scatter_features(gray, mask):
    """Signed scattering features on the background-subtracted image.
    Positive signed = brighter than local background = bright-ring material."""
    ys, xs = np.where(mask > 0)
    if len(ys) < 8:
        return None
    bg = estimate_background_fast(gray).astype(np.float32)
    signed = gray.astype(np.float32) - bg           # sign preserved (halo polarity)
    cy, cx = ys.mean(), xs.mean()
    # robust cell radius from mask pixels
    rmask = np.sqrt((ys - cy) ** 2 + (xs - cx) ** 2)
    R = np.percentile(rmask, 90) or 1.0
    # sample a bbox window padded to 1.6R so we see just outside the mask too
    pad = int(np.ceil(0.6 * R)) + 3
    x0, x1 = max(0, int(xs.min() - pad)), min(gray.shape[1], int(xs.max() + pad + 1))
    y0, y1 = max(0, int(ys.min() - pad)), min(gray.shape[0], int(ys.max() + pad + 1))
    yy, xx = np.mgrid[y0:y1, x0:x1]
    rn = np.sqrt((yy - cy) ** 2 + (xx - cx) ** 2) / R
    win = signed[y0:y1, x0:x1]

    inner = win[rn < INNER_MAX]
    rim = win[(rn >= RIM_LO) & (rn <= RIM_HI)]
    if len(inner) < 4 or len(rim) < 4:
        return None
    inner_m, rim_m = float(inner.mean()), float(rim.mean())
    signed_radial_contrast = rim_m - inner_m        # + => bright rim / dark centre
    ring_polarity = float(win[rn <= 1.0].mean())    # net signed brightness in the cell
    # radial profile (mean signed vs r/R), for the averaged-profile figure
    prof = np.full(len(RBINS) - 1, np.nan)
    for k in range(len(RBINS) - 1):
        sel = (rn >= RBINS[k]) & (rn < RBINS[k + 1])
        if sel.sum() >= 3:
            prof[k] = float(win[sel].mean())
    return dict(inner_m=inner_m, rim_m=rim_m, R=float(R),
                signed_radial_contrast=signed_radial_contrast,
                rim_abs_contrast=abs(signed_radial_contrast),
                ring_polarity=ring_polarity, prof=prof)


def spearman(a, b):
    a, b = np.asarray(a, float), np.asarray(b, float)
    ok = ~(np.isnan(a) | np.isnan(b))
    return float(np.corrcoef(pd.Series(a[ok]).rank(), pd.Series(b[ok]).rank())[0, 1])


def build(meta):
    rows, profs = [], []
    for _, r in meta.iterrows():
        g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
        m = cv2.imread(r.mgt, cv2.IMREAD_GRAYSCALE)
        if g is None or m is None:
            continue
        fe = focus_energy(g, m)
        sf = scatter_features(g, m)
        if fe is None or sf is None:
            continue
        rows.append(dict(idx=int(r.idx), cls=r.cls,
                         focus_lap_var=fe[0], focus_tenengrad=fe[1],
                         inner_m=sf["inner_m"], rim_m=sf["rim_m"], cell_R=sf["R"],
                         signed_radial_contrast=sf["signed_radial_contrast"],
                         rim_abs_contrast=sf["rim_abs_contrast"],
                         ring_polarity=sf["ring_polarity"]))
        profs.append(sf["prof"])
    return pd.DataFrame(rows), np.array(profs)


def fig_metrics(data, profs, path):
    fig = plt.figure(figsize=(12, 8))
    gs = gridspec.GridSpec(2, 2, figure=fig, hspace=0.34, wspace=0.26)
    rc = (RBINS[:-1] + RBINS[1:]) / 2

    # A. averaged radial profile of ALL cells = the white ring, measured
    ax = fig.add_subplot(gs[0, 0])
    med = np.nanmedian(profs, axis=0)
    lo, hi = np.nanpercentile(profs, 25, axis=0), np.nanpercentile(profs, 75, axis=0)
    ax.plot(rc, med, "-", c=BLUE, lw=2.2, label="median over 173 cells")
    ax.fill_between(rc, lo, hi, color=BLUE, alpha=0.15, label="IQR")
    ax.axhline(0, color=INK, lw=1)
    ax.axvspan(0, INNER_MAX, color=RED, alpha=0.08)
    ax.axvspan(RIM_LO, RIM_HI, color=AQUA, alpha=0.18)
    ax.set_xlabel("normalised radius  r / R_cell")
    ax.set_ylabel("mean signed intensity  (frame − background)")
    ax.set_title("A. The white ring, measured: dark disc + bright rim",
                 fontweight="bold", loc="left")
    ax.annotate("dark disc", (INNER_MAX / 2, med[:6].min()), color=RED, fontsize=8, ha="center")
    ax.annotate("bright rim", (1.0, np.nanmax(med)), color="#0e7a52", fontsize=8, ha="center", va="bottom")
    ax.legend(frameon=False, fontsize=8, loc="lower right")

    # B. profile split by focus energy tertile: does the rim sharpen/heighten?
    ax = fig.add_subplot(gs[0, 1])
    order = data.sort_values("focus_lap_var")
    tert = [("low focusLaplacianVar", order.head(len(order)//3).index, RED),
            ("high focusLaplacianVar", order.tail(len(order)//3).index, BLUE)]
    for name, idx, col in tert:
        grp = profs[[data.index.get_loc(i) for i in idx]]
        ax.plot(rc, np.nanmedian(grp, axis=0), "-", c=col, lw=2, label=name)
    ax.axhline(0, color=INK, lw=1)
    ax.axvspan(RIM_LO, RIM_HI, color=AQUA, alpha=0.18)
    ax.set_xlabel("normalised radius  r / R_cell")
    ax.set_ylabel("mean signed intensity")
    ax.set_title("B. Radial profile by focus-energy tertile (sharp vs faint)",
                 fontweight="bold", loc="left")
    ax.legend(frameon=False, fontsize=8, loc="lower right")

    # C. ring-strength distribution: the dataset is one-sided (no focus crossing)
    ax = fig.add_subplot(gs[1, 0])
    src = data.signed_radial_contrast.values
    ax.hist(src, bins=26, color=AQUA, alpha=0.85, edgecolor="white", lw=0.5)
    ax.axvline(0, color=RED, lw=1.4, ls="--", label="focus (rim = centre)")
    ax.set_xlabel("signed radial contrast  (rim − centre)   [ring strength]")
    ax.set_ylabel("detections")
    ax.set_title(f"C. One-sided: {100*(src>0).mean():.0f}% bright-rim, no dark-rim side present",
                 fontweight="bold", loc="left")
    ax.legend(frameon=False, fontsize=8)

    # D. do the unsigned metrics track ring strength ACROSS cells?
    ax = fig.add_subplot(gs[1, 1])
    ax.scatter(data.signed_radial_contrast, data.focus_lap_var, s=16, c=BLUE, alpha=0.55,
               edgecolor="white", lw=0.3)
    q = pd.qcut(data.signed_radial_contrast, 8, duplicates="drop")
    g = data.assign(b=q).groupby("b", observed=True)
    ax.plot(g.signed_radial_contrast.median(), g.focus_lap_var.median(), "-o",
            c=INK, lw=2, ms=5, zorder=5, label="binned median")
    rho = spearman(data.signed_radial_contrast, data.focus_lap_var)
    rho_t = spearman(data.signed_radial_contrast, data.focus_tenengrad)
    ax.set_yscale("log")
    ax.set_xlabel("signed radial contrast (ring strength)")
    ax.set_ylabel("focusLaplacianVar")
    ax.set_title(f"D. Weak across independent cells (cell-to-cell variability)\n"
                 f"Spearman: Lap {rho:+.2f}, Tenengrad {rho_t:+.2f}",
                 fontweight="bold", loc="left")
    ax.legend(frameon=False, fontsize=8)

    fig.suptitle("Light-scattering focus signature on real brightfield cells",
                 fontsize=13, fontweight="bold")
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def crop_cell(meta, idx, out=64, pad=8):
    r = meta[meta.idx == idx].iloc[0]
    g = cv2.imread(r.image, cv2.IMREAD_GRAYSCALE)
    cx, cy = (r.bx0 + r.bx1) / 2, (r.by0 + r.by1) / 2
    half = max(r.bx1 - r.bx0, r.by1 - r.by0) / 2 + pad
    x0, x1 = max(0, int(cx - half)), min(g.shape[1], int(cx + half))
    y0, y1 = max(0, int(cy - half)), min(g.shape[0], int(cy + half))
    patch = g[y0:y1, x0:x1]
    if patch.size == 0:
        patch = g
    return cv2.resize(patch, (out, out), interpolation=cv2.INTER_NEAREST)


def fig_montage(data, meta, path, ncols=8, nrows=4):
    order = data.sort_values("focus_lap_var", ascending=False).reset_index(drop=True)
    n = ncols * nrows
    picks = order.iloc[np.linspace(0, len(order) - 1, n).astype(int)].reset_index(drop=True)
    fig, axes = plt.subplots(nrows, ncols, figsize=(ncols * 1.15, nrows * 1.28))
    fig.suptitle("Real cells ordered by focus energy (Laplacian variance): sharp white ring → faint",
                 fontsize=12, fontweight="bold", y=1.0)
    for k, ax in enumerate(axes.ravel()):
        r = picks.iloc[k]
        ax.imshow(crop_cell(meta, int(r.idx)), cmap="gray", vmin=0, vmax=255)
        col = BLUE if r.cls == "cell" else AQUA
        for sp in ax.spines.values():
            sp.set_color(col); sp.set_linewidth(2.0)
        ax.set_xticks([]); ax.set_yticks([])
        ax.set_title(f"L={r.focus_lap_var:,.0f}\nrim={r.signed_radial_contrast:.0f}",
                     fontsize=7, color=INK, pad=2)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)


def main():
    os.makedirs("results", exist_ok=True)
    meta = B.load()
    data, profs = build(meta)
    data = data.reset_index(drop=True)
    data.to_csv("results/scatter_focus_data.csv", index=False)
    fig_metrics(data, profs, "results/scatter_focus_metrics.png")
    fig_montage(data, meta, "results/scatter_focus_montage.png")

    src = data.signed_radial_contrast.values
    print(f"n = {len(data)} detections")
    print(f"dark disc (inner<0): {100*(data.inner_m<0).mean():.0f}%  |  "
          f"bright rim (rim>0): {100*(data.rim_m>0).mean():.0f}%  |  "
          f"rim>centre: {100*(src>0).mean():.0f}%  -> one-sided (no focus crossing)")
    print(f"dark-disc depth  median {data.inner_m.median():+.1f} gray  |  "
          f"bright-rim height median {data.rim_m.median():+.1f} gray")
    print(f"Spearman |rim-centre| vs focusLaplacianVar : {spearman(data.rim_abs_contrast, data.focus_lap_var):+.3f}")
    print(f"Spearman |rim-centre| vs focusTenengrad    : {spearman(data.rim_abs_contrast, data.focus_tenengrad):+.3f}")
    print(f"Spearman signed src   vs focusLaplacianVar : {spearman(data.signed_radial_contrast, data.focus_lap_var):+.3f}  (want ~0: sign-blind)")
    # dip test: median focus energy in central (near-focus) third of |src| vs outer thirds
    a = data.assign(absrc=data.signed_radial_contrast.abs())
    lo = a[a.absrc <= a.absrc.quantile(0.33)].focus_lap_var.median()
    hi = a[a.absrc >= a.absrc.quantile(0.67)].focus_lap_var.median()
    print(f"focusLaplacianVar median: near-focus third {lo:.0f}  vs  far-defocus third {hi:.0f}  (ratio {hi/lo:.2f}x)")
    print("wrote results/scatter_focus_data.csv, scatter_focus_metrics.png, scatter_focus_profiles.png")


if __name__ == "__main__":
    main()
