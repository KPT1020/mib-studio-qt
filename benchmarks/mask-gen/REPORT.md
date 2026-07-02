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

## Caveats

* Reference masks are SAM2 pseudo-GT (`gt_status = predicted`), not
  hand-labelled; IoU is measured against that target, i.e. "how well can a
  200 µs classical pipeline reproduce the SAM2 mask", not against pixel-truth.
* Timings are single-thread Python/OpenCV on this benchmark host; they use the
  same native kernels as the C++ app and are indicative, not a promise for
  specific production hardware. The C++ path avoids per-call Python overhead
  and the app runs multiple processing workers, so real headroom is larger.
