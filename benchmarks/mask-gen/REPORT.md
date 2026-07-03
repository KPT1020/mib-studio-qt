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
repeated single-thread runs on the real 512×96 strips, including
`findContours` + fill (same OpenCV kernels as the C++ app). Latencies in
this and the next few sections assume a *precomputed* background; the
**Real-stream validation** section below gives the sustained cost with the
per-frame background included (the authoritative throughput number).

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

### Adoption status

**Landed** (2026-07-03): the per-frame **Otsu threshold**, gated by
`ProcessingConfig::adaptive_threshold` (+ `otsu_scale`), via a shared
`applyProcessingThreshold` helper used by `computeProcessedFrame` and all three
realtime-loop copies. It is floored at `bg_subtract_threshold` so empty ROIs
stay empty, defaults off (no behaviour change), and is covered by
`tests/processing/processing_adaptive_threshold_test.cpp`.

**Landed** (2026-07-03): **decoupled size measurement**, so enabling adaptive
detection does not drift the size-derived analysis. Because `area`, `areaRatio`
and `deformability` (hence `youngsModulus` and the area/target-group gates) are
read from the segmentation contour, the tighter per-frame Otsu mask would move
them with contrast/scene content. Detection now stays on the Otsu mask while
those metrics are re-measured on a fixed-threshold *measurement mask*
(`buildMeasurementMask` = the adaptive-off mask), per object via
`matchContourByOverlap`, with fallback to the adaptive contour. Tied to
`adaptive_threshold` (no new knob); test
`tests/processing/processing_decoupled_measurement_test.cpp`.

### Decoupled measurement — GT validation (`decoupled_bench.py`)

Ran the safeguard over the 173-object GT set (C++ defaults: blur 3, morph 3,
`bg_subtract_threshold` 8, `otsu_scale` 1.1). Three findings, all decision-relevant:

1. **On this pipeline, adaptive Otsu does not improve detection.** Co-located
   detection recall is **94.8% fixed vs 93.6% adaptive** (−1.2 pts) at the
   default config, and adaptive never wins across a morph/threshold sweep. The
   large detection gain in this report (98% vs 69%) belongs to the *proposed*
   pipeline (CLAHE/top-hat + Otsu), **not** the current-pipeline Otsu swap. So
   flipping `adaptive_threshold` on by itself buys little detection.
2. **The object↔measurement match must be overlap-based, not centroid
   containment.** The fixed mask is often fragmented/offset for dim cells, so a
   strict point-in-polygon test on the object centroid fell back **61.7%** of the
   time and, when it did match, grabbed fragments (p90 area error **40%**).
   Switching to intersection-over-min-area overlap (`matchContourByOverlap`)
   dropped fallback to **0%** and put **100%** of objects within 2% of the fixed
   basis (p90 error **0%**). The C++ helper was changed to overlap accordingly.
3. **How much drift the safeguard removes is config-dependent.** At morph 3 the
   adaptive and fixed areas already agree (p90 drift ~6%), so there is little to
   fix; the drift only becomes large at heavier morphology (morph 5: adaptive-raw
   p90 drift **74%**), where decoupling cleanly restores the fixed basis (p90
   **0–1%**). The safeguard therefore earns its cost specifically for the
   REPORT LEAN adaptive config (5×5 close), and is near-neutral at the current
   default.

Net: keep `adaptive_threshold` **off** by default (unchanged, byte-identical).
The decoupling is a correct, graceful safeguard — it degrades to prior behaviour
via fallback and is exact when it engages — but its benefit is realized only if
adaptive is adopted with larger morphology. Data: `results/decoupled_area.csv`.

### Area accuracy vs GT — the measurement basis is inaccurate (`area_accuracy.py`)

The decoupling keeps `area` on the fixed-threshold basis — but "stable" is only
useful if the basis is also *accurate*. Scored each pipeline's cell area against
the SAM2 GT area (physical truth), with IoU / detection alongside:

| pipeline | mean IoU | det@0.5 | area err median | area err p90 |
|---|---:|---:|---:|---:|
| current  FIXED    (adaptive off)    | 0.343 | 20.8% | **49.0%** | 85.2% |
| current  ADAPTIVE (Otsu, shipped)   | 0.331 | 19.1% | **49.5%** | 88.4% |
| proposed LEAN     (absdiff+Otsu, m5)| 0.849 | 98.3% | **10.0%** | 27.5% |
| proposed OPT      (+ rect top-hat)  | 0.849 | 98.3% | **10.0%** | 27.5% |

The current pipeline — fixed *or* adaptive — mis-measures cell area by ~49%
against truth; the proposed pipeline by ~10%. So the fixed-threshold basis the
decoupling stabilises onto is itself badly inaccurate, and the adaptive path we
gated the safeguard behind is no better than fixed. Crucially the gap is **not**
morphology: bumping the current-adaptive path to a 5×5 close *collapses* it (IoU
0.10, 0% detection) — the accuracy lives in the proposed front-end (absdiff +
the ROI/Otsu interaction, optional top-hat), which is not reachable by tuning the
current pipeline. Area accuracy for downstream analysis therefore needs the
**proposed pipeline ported to C++**, not a measurement remap. Because the
proposed pipeline's area is already accurate (~10% vs GT) and self-consistent,
it would not need the fixed-threshold decoupling. Adopting it changes absolute
area values ~40%, so it requires re-calibrating `area_threshold_*`, the
target-group windows, and the E-modulus LUT. Data: `results/area_accuracy.csv`.

### Prototype — C++ A/B, real-time performance (`processing_proposed_pipeline_bench`)

Ported the proposed front-end into `computeProcessedFrame` behind
`ProcessingConfig::proposed_pipeline` and benchmarked current vs proposed in the
**actual C++ path** on the GT set (per-row-median background, channel band, 20
reps/frame). The entire accuracy gap is one operator — `cv::absdiff` instead of
signed `cv::subtract` — which captures the whole cell footprint, not just the
brighter-than-background part; no CLAHE/bilateral/top-hat needed on flat fields:

| pipeline (C++) | mean IoU | det@0.5 | area err median | latency median |
|---|---:|---:|---:|---:|
| current  (subtract, fixed)      | 0.343 | 20.8% | 48.2% | 0.148 ms |
| proposed (absdiff, Otsu, m5)    | 0.811 | 97.7% | 16.0% | 0.163 ms |

**Real-time cost is negligible: 1.10× latency** (~0.16 ms/frame, ~6000 fps) for
**2.4× IoU, ~5× detection, ~3× better area accuracy**. The C++ numbers track the
Python prototype (IoU 0.81 vs 0.85; the small gap is the median-vs-nth_element
background and the full validation path). The bench skips when the dataset is
absent (CI-safe) and asserts proposed IoU > current otherwise. Flag is off by
default and wired through `config.json` / `AppConfigWatcher`
(`proposed_pipeline`, `proposed_tophat_kernel`).

**Follow-ups:** re-calibrate the area gates + E-modulus LUT for the new masks
(area values shift ~40%); apply `proposed_pipeline` to the realtime-loop copies
(prototype covers the shared `computeProcessedFrame` / batch / playback path);
per-row-mean background source; settings-dialog widgets.

### C++ adoption sketch (`ProcessingService::computeProcessedFrame`)

The current ROI body does GaussianBlur → `cv::subtract` → fixed
`cv::threshold` → close → open. The change is local — swap the fixed
threshold for Otsu and add the optional top-hat:

```cpp
// instantaneous per-row-mean background over the ROI band (drift-proof, ~6 us)
cv::Mat rowMean; cv::reduce(roiCurr, rowMean, 1, cv::REDUCE_AVG, CV_8U);
cv::Mat bg; cv::repeat(rowMean, 1, roiCurr.cols, bg);
cv::absdiff(roiCurr, bg, diff);
if (cfg.enable_tophat) {                                     // optional (abnormal drift)
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

Two things the real-stream validation made concrete: (1) the background is a
per-row **mean** over the ROI band, recomputed each frame — instantaneous, so
it tracks drift for free and needs no captured/rolling background frame; and (2)
Otsu **must** run on the ROI band only (the production code already scopes work
to `cvRoi`) — a whole-frame Otsu would be dragged by the channel walls. Landing
this in the C++ path is a follow-up: it changes runtime segmentation behaviour
and per the repo rules needs its pipeline-e2e + latency-budget tests and vault
updates, plus a config flag + migration for the now-adaptive threshold. This
benchmark validates the method and settles the parameters first.

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

### Round-2 recommendation (later superseded)

Round 2 recommended an EWMA running-average background, because under a large
*synthetic* drift a stale background collapsed while an adapting one held up.
The **real-stream validation below supersedes that** — on genuine video the
drift is mild and an instantaneous per-row background wins. Kept here for the
audit trail.

## Real-stream validation (authoritative)

The round-1/2 numbers use the 173-detection GT set, which is *independent*
frames; the drift tests there had to *synthesise* a history. The full
**`gavinlouuu/512x96stream`** (5000 consecutive frames of the same channel)
lets us test on real video: all 171 GT frames are located in the stream by
exact pixel hash (`download_stream.py`), so temporal backgrounds use the actual
preceding frames and throughput is measured on a real sequence
(`stream_bench.py`, `results/stream_experiments.csv`).

Real drift across the matched span (stream idx 20–4807) is only **~3 gray
levels** — far smaller than the ±14 synthetic ramp. Consequences:

| background (OPT pipeline) | IoU | det | note |
|---------------------------|:---:|:---:|------|
| **per-row mean** (instantaneous, `cv2.reduce`) | 0.848 | **100 %** | recomputed each frame → drift-proof for free |
| per-row median (instantaneous)    | 0.849 | 98 % | equal accuracy, 33× slower to compute |
| captured frame#0 (stale)          | 0.825 | 95 % | the only case drift actually costs anything |
| temporal median K=30 (real hist)  | 0.808 | 93 % | **worse** — per-pixel, noisier, picks up transient cells |
| EWMA α=0.02 (real hist)           | 0.819 | 94 % | **worse** — same reason |

**The temporal-background idea does not survive contact with real data.** An
*instantaneous* per-row background is recomputed from the current frame, so it
already tracks drift with zero lag and beats every rolling/EWMA/MOG2 variant,
which are noisier and cell-contaminated. The earlier drift concern was an
artifact of pairing a *stale* background with an oversized synthetic ramp.

Sustained per-frame latency **including** the background (single thread, real
stream, incl. `findContours` + fill) — earlier sections had excluded background
cost by assuming a stored frame:

| pipeline | µs/frame | fps |
|----------|:--------:|:---:|
| `np.median` background alone | 336 | — (avoid: 33× too slow) |
| `cv2.reduce` row-mean background alone | 6 | — |
| full OPT **with** top-hat + row-mean bg | 218 | 4600 |
| **full, NO top-hat + row-mean bg**      | **180** | **5560** |

On real data the top-hat adds **nothing** (0.848 with and without) because the
mild drift leaves no residual shading to flatten, and it costs ~40 µs. So the
budget-compliant real-stream pipeline drops it.

### Final recommendation (real-stream)

```
ROI band (channel interior; auto_refine_band snaps a coarse user ROI)
  → background = per-row MEAN via cv2.reduce   (instantaneous, ~6 µs, drift-proof)
  → absdiff(frame, background)
  → Otsu threshold (× ~1.1)                    (per-frame adaptive)
  → morphological close 5×5                    (rim → filled disk)
  → morphological open 3×3
  → findContours + fill
```

**0.848 IoU / 100 % detection / ~180 µs (5560 fps) single-thread Python**,
C++ faster still. The top-hat becomes an optional flag for abnormal drift;
temporal backgrounds, hysteresis, watershed, and shape regularisation are not
worth it here. Use `pipelines.estimate_background_fast` for the background.

## Focus metric — replacing the nested-contour ring ratio

`ProcessingService::calculateRingRatio` = `sqrt(parentArea − innerArea)` from a
**nested** contour pair (`findContours(RETR_TREE)`), used to quantify cell
focus. It only exists when the cell thresholds into a *closed* ring, so it
breaks whenever the rim is not a closed loop. Measured on this GT set
(`focus_metric.py`):

* **Robustness** — the nested ring is present on only **66 %** of frames with
  the fixed threshold and **73 %** with adaptive Otsu. (So the adaptive-Otsu
  change does *not* degrade the ring — it is marginally more robust — but ~1/3
  of frames still have no usable ring.)

Focus lives in the cell's **intensity**, not in mask topology. Computing it from
the background-subtracted intensity inside a *solid* cell mask (the Otsu-filled
region — always available) decouples "where is the cell" from "is it focused"
and is defined on **100 %** of frames. Candidates, validated by a controlled
defocus sweep (blur the 40 sharpest cells, σ 0→3; a good focus metric falls
monotonically with large dynamic range):

| metric | defined | defocus dynamic range | Spearman vs ref ring_ratio |
|--------|:-------:|:---------------------:|:--------------------------:|
| **variance of Laplacian** | 100 % | **50.9×**, monotonic | +0.40 |
| Tenengrad (Sobel energy)  | 100 % | 10.9×, monotonic | +0.39 |
| intensity std             | 100 % | 2.0× (weak) | +0.36 |
| radial contrast ratio     | 100 % | unstable (sign flips) | +0.33 |
| old nested-ring ratio     | 66–73 % | — (undefined on 1/3) | reference |

**Variance of the Laplacian** is the clear winner: always defined, monotonic
under defocus with a 50× range, and cheap (one `cv::Laplacian` + `meanStdDev`
over the object mask, a few µs). Correlation to the old ring_ratio is only
moderate (+0.40) because that reference is itself the noisy quantity being
replaced (0 on 11/173 even in the SAM2 pipeline) — the defocus sweep is the
trustworthy validation. Recorded in `results/focus_experiments.{csv,json}`.

**Recommended change:** add a topology-free `focusScore` = variance of the
Laplacian of the bg-subtracted ROI within the object mask, as a *new* field
alongside `ringRatio` (additive, so existing configs keep working), then migrate
the focus gate from `ringRatio` to `focusScore`:

```cpp
// bg-subtracted ROI `diff` (CV_8UC1) and the filled object mask `objMask`
cv::Mat lap; cv::Laplacian(diff, lap, CV_32F, 3);
cv::Scalar m, sd; cv::meanStdDev(lap, m, sd, objMask);
double focusScore = sd[0] * sd[0];   // variance of Laplacian within the cell
```

## Caveats

* Reference masks are SAM2 pseudo-GT (`gt_status = predicted`), not
  hand-labelled; IoU is measured against that target, i.e. "how well can a
  200 µs classical pipeline reproduce the SAM2 mask", not against pixel-truth.
* Timings are single-thread Python/OpenCV on this benchmark host; they use the
  same native kernels as the C++ app and are indicative, not a promise for
  specific production hardware. The C++ path avoids per-call Python overhead
  and the app runs multiple processing workers, so real headroom is larger.
