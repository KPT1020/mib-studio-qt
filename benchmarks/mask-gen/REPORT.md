# Real-time mask-generation pipeline — benchmark & recommendation

**Goal.** Segment flowing cells at **5000 fps (≤ 200 µs/frame)** while
reproducing the accuracy of the slow YOLO+SAM2 reference masks
(`mask_gt`), using the classical chain
`absdiff → CLAHE → bilateral → DoG/Top-hat → Otsu → close → findContours`,
and compare against the current production pipeline.

**Dataset.** `gavinlouuu/biowork-mask-gen-benchmark` — 173 detections
(144 `cell`, 29 `cluster`) across 171 microfluidic strips, 512×96, 8-bit.
Reference = the accepted SAM2 mask per detection.

**Scoring.** Per-detection IoU / Dice inside the (padded) bounding box;
masks contour-filled to match the filled reference. Latency = median of
repeated single-thread runs on the real 512×96 strips, background
precomputed, including `findContours` + fill (same OpenCV kernels as the
C++ app).

## Headline result

| Pipeline | mean IoU | median IoU | Dice | detect@0.5 | IoU ≥ 0.7 | µs/frame | fps |
|----------|:--------:|:----------:|:----:|:----------:|:---------:|:--------:|:---:|
| current — default (`thresh=8`)        | 0.343 | 0.260 | 0.463 | 20.8 % | 19.1 % |  87 | 11 500 |
| current — best-tuned (`thresh=2`)     | 0.520 | 0.606 | 0.654 | 68.8 % | 16.2 % | 140 |  7 200 |
| proposed — **FULL literal spec**      | 0.727 | 0.768 | 0.831 | 91.9 % | 65.3 % | 501 |  2 000 |
| proposed — **OPT** (tuned, top-hat)   | **0.849** | 0.885 | 0.915 | 98.3 % | 93.1 % | 142 |  7 000 |
| proposed — **LEAN** (absdiff+Otsu)    | 0.849 | 0.885 | 0.915 | 98.3 % | 93.1 % | **109** | **9 200** |

`comparison.png` (rows: *image / current-best / proposed-opt / GT*;
green = hit, red = missed, yellow = false-positive) shows the proposed
pipeline tracking the reference cleanly, recovering faint cells the current
pipeline drops, and shedding the center-line false positives the current
pipeline fires on.

## Findings

1. **The current pipeline is accuracy-limited, not speed-limited.** Its fixed
   global threshold on a signed background subtraction cannot adapt to
   per-cell contrast: even at its best threshold it reaches only IoU 0.52 /
   69 % detection. Otsu (per-frame adaptive) is the decisive change.

2. **The literal proposed chain fails *both* targets.** At 501 µs (2000 fps)
   it is 2.5× over budget, and at IoU 0.727 it is *worse* than the pruned
   version. Cause (see `ablation.py`):
   * **CLAHE hurts** (155 µs, and it amplifies noise in the near-zero
     absdiff background) — removing it *raises* IoU.
   * **Bilateral hurts** (84 µs, over-smooths the cell rim) — removing it
     *raises* IoU.
   * **DoG collapses** here (IoU 0.41): the band-pass suppresses the solid
     cell interior the reference marks as foreground.
   * Top-hat is the only enhancer that helps, and it is the second cost
     driver (119 µs for a 15×15 ellipse).

3. **Background subtraction is doing the heavy lifting.** Because the per-row
   median removes the channel field so well, the minimal chain
   `absdiff → Otsu → close(5×5) → fill` already reaches IoU 0.849 at 9200 fps
   (LEAN). A cheap **separable rect top-hat (21×21)** matches that accuracy
   with margin to spare (OPT, 7000 fps) and adds robustness if the background
   estimate drifts — so it is the recommended production default.

4. **ROI restriction is the biggest single lever** (mean IoU 0.43 → 0.73
   before any tuning) and also cuts cost: it removes the bright frame edges /
   textured walls that create false positives *and* poison the global Otsu
   histogram.

## Per-stage cost (ROI band 58×512, single thread)

| stage | µs | verdict |
|-------|---:|---------|
| absdiff                     |   3 | keep (core) |
| Otsu threshold              |  19 | keep (core) |
| morph close 5×5             |  ~13 | keep (fills rim → disk) |
| rect top-hat 21×21 (separable) | ~40 | keep (cheap robustness) |
| gaussian 3×3                |   8 | optional |
| ellipse top-hat 15×15       | 119 | drop (use rect) |
| bilateral d=5               |  84 | **drop** (hurts IoU) |
| CLAHE 8×8                   | 155 | **drop** (hurts IoU) |
| DoG (σ 1/3)                 | 157 | **drop** (collapses) |

## Recommended real-time pipeline

```
ROI band (channel interior)
  → absdiff(frame, background)          # captured/rolling bg in production
  → white top-hat, 21×21 RECT (separable)   # optional; flattens residual drift
  → Otsu threshold (× ~1.1)             # per-frame adaptive
  → morphological close 5×5             # rim → filled disk
  → morphological open 3×3              # drop single-pixel speckle
  → findContours + fill
```

Config: `pipelines.PROPOSED_OPT_CFG`. Drop the top-hat for the LEAN variant
when maximum throughput matters and the background estimate is trustworthy.

### Why keep the top-hat? (OPT vs LEAN)

On clean data OPT and LEAN are identical (0.849). The top-hat earns its ~40 µs
only when the background estimate is imperfect — the normal live condition
(stale captured background, illumination/thermal drift). Simulating that
(stale background + a horizontal shading ramp on the current frame,
`explore.py` part A):

| pipeline | clean IoU / det | **drifted-bg** IoU / det |
|----------|:---------------:|:------------------------:|
| LEAN (no top-hat)   | 0.849 / 98 % | 0.541 / **43 %** |
| OPT (rect top-hat)  | 0.849 / 98 % | 0.733 / **88 %** |
| OPT + adaptive thr. | 0.809 / 98 % | 0.662 / 80 % |

The top-hat re-flattens residual low-frequency shading so Otsu still sees a
clean bimodal histogram — the difference between 43 % and 88 % detection under
drift. Hence OPT is the recommended default.

### User ROI + auto-refine

The app already lets the user draw the ROI (`RoiDrawCanvas` / `RoiManager`,
persisted via the EGrabber offsets), and that ROI *is* the processing band —
the dataset-specific `12..70` here is just where this channel sits. Rather than
ask the user to place it precisely, `pipelines.auto_refine_band()` snaps a
coarse box to the channel interior (the bright plateau between the two dark
walls) from the row-brightness profile, ~5 µs (`explore.py` part B):

| band | IoU / det |
|------|:---------:|
| fixed hand-set 12..70          | 0.849 / 98 % |
| auto-refine from **full frame**    | 0.819 / 95 % |
| auto-refine from **sloppy 5..90 box** | 0.818 / 95 % |

It recovers `y0≈16, y1≈69` identically across all 171 strips and is robust to a
careless box. Recommended UX: **user draws a rough ROI → auto-refine snaps it to
the channel → pipeline runs on the band.** The small residual gap vs the hand
band is a slightly tighter top edge clipping the largest cells.

### C++ adoption sketch (`ProcessingService::computeProcessedFrame`)

The current ROI body does GaussianBlur → `cv::subtract` → fixed
`cv::threshold` → close → open. The change is local — swap the fixed
threshold for Otsu and add the optional top-hat:

```cpp
cv::absdiff(roiCurr, backgroundGray(cvRoi), diff);           // was cv::subtract
if (cfg.enable_tophat) {                                     // optional
    cv::Mat k = cv::getStructuringElement(cv::MORPH_RECT, {21, 21});
    cv::morphologyEx(diff, diff, cv::MORPH_TOPHAT, k);
}
// per-frame adaptive threshold replaces the fixed bg_subtract_threshold
double t = cv::threshold(diff, thresh, 0, 255,
                         cv::THRESH_BINARY | cv::THRESH_OTSU);
cv::Mat mk = cv::getStructuringElement(cv::MORPH_ELLIPSE, {5, 5});
cv::morphologyEx(thresh, roiDst, cv::MORPH_CLOSE, mk);
cv::morphologyEx(roiDst, roiDst, cv::MORPH_OPEN,
                 cv::getStructuringElement(cv::MORPH_ELLIPSE, {3, 3}));
```

Otsu needs a clean histogram, so it **must** run on the ROI band only (as the
production code already scopes work to `cvRoi`) — a whole-frame Otsu would be
dragged by the channel walls. Landing this in the C++ path is a follow-up: it
changes runtime segmentation behaviour and per the repo rules needs its
pipeline-e2e + latency-budget tests and vault updates, plus a config flag +
migration for the now-adaptive threshold. This benchmark validates the method
and settles the parameters first.

## Other methods explored

Tested (`explore.py` parts C/D, OPT base, fixed band):

| method | IoU | det | µs | note |
|--------|:---:|:---:|:--:|------|
| Otsu (baseline)          | 0.849 | 98 %  | 177 | recommended |
| **Triangle** threshold   | 0.828 | **100 %** | 178 | best recall; looser masks. Triangle suits a small foreground fraction, which is our case — worth a "never-miss" mode |
| Adaptive (mean) thr., no top-hat | 0.809 | 98 % | 175 | needs **no background at all** — good bg-free fallback / drift mode |
| Otsu + fill-holes        | 0.508 | 32 %  | 177 | **hurts** — post-close cells are already solid; flood-fill leaks into channel |
| Triangle + fill-holes    | 0.367 |  1 %  | 153 | **hurts** badly |
| `connectedComponentsWithStats` vs `findContours` | — | — | 240 vs **44** | findContours wins on these small frames; CCA not worth it |

So among drop-in swaps, only the threshold operator is interesting: Otsu for
best IoU, Triangle for 100 % recall, adaptive when no background exists.

## Method prototypes — benchmarked (round 2)

All prototyped and scored against the same GT; full numbers in
`results/experiments.{csv,json}` (reproduce with `python run_experiments.py`).
Baseline for comparison is OPT: **0.849 IoU / 98 % / 162 µs** clean,
**0.733 / 88 %** under drift.

### 1. Background models — the standout win

| background | clean IoU / det | drift IoU / det | per-frame cost |
|------------|:---------------:|:---------------:|----------------|
| per-row median (current)     | 0.849 / 98 % | 0.733 / 88 % | cheap |
| row mean                     | 0.843 / **100 %** | 0.727 / 86 % | cheap |
| large Gaussian (k=51)        | 0.827 / 98 % | **0.753 / 92 %** | cheap |
| morph-open k=25 / poly-fit   | 0.41 / 0.62  | worse | — (kernel < cell → eats cells) |
| MOG2, clean / adapted-history | 0.845 / 99 % | **0.845 / 99 %** | **512 µs — over budget** |
| MOG2, stale history          | — | 0.628 / 64 % | — |
| KNN                          | 0.698 | 0.66–0.70 | — |
| **EWMA running-average**     | 0.843 / 99 % | **0.843 / 99 %** | **~absdiff (few µs)** |

The clear result: a **continuously-adapting background neutralises drift at the
source** — MOG2 and a cheap **EWMA running average** both fully recover under
drift (0.84, vs 0.73 for static-bg + top-hat) at parity on clean data. MOG2's
per-pixel GMM costs 512 µs (over budget); the **EWMA running average gets the
same accuracy for a few µs** and is the recommended upgrade to the background
stage — production has the live stream to feed it. (Fairness note: the temporal
tests use a *synthesised* per-strip history since the GT has no video; the
"adapted-history" column is the fair mechanism test.) A large-Gaussian
background is a simpler middle ground: slightly better than row-median under
drift, still cheap.

### 2–4. Post-processing methods — all neutral-to-negative here

| method | IoU | det | µs | verdict |
|--------|:---:|:---:|:--:|---------|
| Hysteresis (double) threshold | 0.69–0.78 | up to 99 % | 420–510 | worse IoU than single Otsu **and** over budget; the low arm admits wall/noise |
| Watershed (cluster split)     | 0.78–0.82 | 93–97 % | ~400 | lowers IoU; on the 2 multi-detection strips it **drops 0.79 → 0.57**. Clusters are labelled as *single* objects, so splitting fights the GT |
| Shape reg — ellipse / hull    | 0.843 / 0.842 | 99 % | 347 / 215 | no gain: close(5×5) masks are already blob-like |

None beat OPT's 0.849; recorded so they aren't re-tried blindly. (Watershed
stays useful if the labelling ever treats touching cells as separate instances —
not the case in this dataset.)

### 5. Throughput

| path | µs/frame | note |
|------|:--------:|------|
| single thread (1 core)        | 162 | baseline |
| OpenCV threads = all          | 165 | no help — frame too small, threading overhead dominates |
| **strip-batching K=16**       | **58** | 2.8× — amortises per-call overhead across stacked strips (linear stages; Otsu stays per-strip) |
| GPU / CUDA                     | —   | 0 devices on this host |

Batching is the real throughput lever if the ROI grows or heavier stages are
added; multi-threading and GPU don't help at this frame size.

### 6. Distilled tiny CNN — **not run**

A quantised U-Net distilled from the SAM2 masks needs a training pipeline + GPU;
it's a separate track, recorded as not run. Only worth it if classical accuracy
must exceed ~0.85 IoU.

### Updated recommendation

Keep the OPT pipeline, and upgrade the background from the static per-row median
to an **EWMA running-average background fed by the live stream** — this handles
illumination/thermal drift at the source (0.84 under drift vs 0.73), costs a few
µs, and could even let the top-hat be dropped. Add **strip-batching** if
throughput headroom is needed. Skip hysteresis, watershed, and shape
regularisation for this dataset.

## Caveats

* Reference masks are SAM2 pseudo-GT (`gt_status = predicted`), not
  hand-labelled; IoU is measured against that target, i.e. "how well can a
  200 µs classical pipeline reproduce the SAM2 mask", not against pixel-truth.
* Timings are single-thread Python/OpenCV on this benchmark host; they use the
  same native kernels as the C++ app and are indicative, not a promise for
  specific production hardware. The C++ path avoids per-call Python overhead
  and the app runs multiple processing workers, so real headroom is larger.
