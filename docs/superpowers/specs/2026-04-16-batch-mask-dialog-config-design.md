# Design: Processing Config Panel in BatchMaskDialog

**Date:** 2026-04-16  
**Status:** Approved

## Context

`BatchMaskDialog` currently uses the live pipeline's `ProcessingConfig` unchanged for
every batch run. Users need to tune mask generation parameters (blur, threshold,
morphology, validation bounds) per-run without leaving the dialog or touching the live
pipeline. Adding a config panel directly in the dialog shortens the iteration cycle:
adjust → preview frame → run → see result, all in one place.

## Scope

Add a third column "Processing Config" to `BatchMaskDialog` exposing:

**Image Processing:**
- Gaussian blur kernel size (`gaussian_blur_size`)
- BG subtract threshold (`bg_subtract_threshold`)
- Morph kernel size (`morph_kernel_size`)
- Morph iterations (`morph_iterations`)

**Validation:**
- Area min / max in μm² (`area_threshold_min`, `area_threshold_max`)
- Deformability min / max (`deformability_threshold_min`, `deformability_threshold_max`)

## Layout

```
[Source & Output (left, stretch=1)] | [Preview & ROI (center, stretch=1)] | [Processing Config (right, ~230px fixed)]
```

Dialog resizes from 950 → ~1180 px wide. Height unchanged.

The config column contains:
- `QGroupBox "Image Processing"` with `QFormLayout`
  - Blur kernel: `QSpinBox` range 1–99, step 2 (odd values only)
  - BG threshold: `QSpinBox` range 0–255
  - Morph kernel: `QSpinBox` range 1–99, step 2
  - Morph iterations: `QSpinBox` range 1–20
- `QGroupBox "Validation"` with `QFormLayout`
  - Area min (μm²): `QDoubleSpinBox` range 0–10 000, decimals 1
  - Area max (μm²): `QDoubleSpinBox` range 0–10 000, decimals 1
  - Deform min: `QDoubleSpinBox` range 0.0–1.0, step 0.001, decimals 4
  - Deform max: `QDoubleSpinBox` range 0.0–1.0, step 0.001, decimals 4
- `QPushButton "Reset to live defaults"` — re-reads `getProcessingConfig()` and repopulates all spinboxes

## Data Flow

1. On dialog construction: `localConfig_` (`ProcessingConfig`) is initialised from
   `backend_.processing().getProcessingConfig()`. Spinboxes are set to match.
2. Each spinbox's `valueChanged` signal updates the corresponding field in `localConfig_`
   directly (no intermediate Apply button).
3. `onRun()` replaces `proc.getProcessingConfig()` with `localConfig_` — no other
   changes to the run path.
4. "Reset" button: calls `resetConfigToLive()` helper which re-reads and repopulates.
5. **Live pipeline is never written** — all edits are local to this batch run.

## Files Changed

| File | Change |
|------|--------|
| `include/frontend/dialogs/BatchMaskDialog.h` | Add `localConfig_`, 8 spinbox members, `resetConfigToLive()` helper |
| `src/frontend/dialogs/BatchMaskDialog.cpp` | `buildUi()` adds third column; `onRun()` uses `localConfig_`; new helper |

No new files required.

## Verification

1. Build Debug.
2. Open dialog with HDF5 source — confirm spinboxes match live `ProcessingConfig`.
3. Change blur kernel and area max, click Run — confirm log shows correct processing.
4. Click "Reset to live defaults" — spinboxes revert to live pipeline values.
5. Confirm live pipeline config is unchanged after dialog closes.
