# Design: BatchMaskDialog Always Produces Standard HDF5 Output

**Date:** 2026-04-16  
**Status:** Approved

## Context

`BatchMaskDialog` currently has a separate "Output" group box with three independent
options: "Display in review tab" (in-memory only), "Save mask PNGs", and "Save masks to
HDF5 file". The in-memory display path bypasses `HdfReviewTab::loadHdfFile()`, so
scatter plot, histogram, metadata table, and ROI overlay do not update after a batch run.
Users must manually open the saved file to get full functionality.

## Goal

Simplify: every batch run always saves a complete, standard HDF5 file next to the source,
then reloads `HdfReviewTab` from that file. All downstream features (scatter plot,
histogram, metadata table, thumbnails, export) work immediately after the run.

## Output Path

Auto-computed from the input source — no user input required:

| Source type | Output path |
|-------------|-------------|
| HDF5 file   | `<same_dir>/<source_stem>_remasked.h5` |
| Folder      | `<folder>/<folder_name>_remasked.h5` |

If the computed path already exists the user is prompted to confirm overwrite
(`QMessageBox::question`). If they decline, the run is cancelled (no processing).

## Run Flow

1. User configures source, ROI, background, processing config → clicks **Run**.
2. If output path already exists → prompt overwrite. Cancel if declined.
3. `processBatch()` runs; log and progress bar update as before.
4. `saveMasksToHdf5()` saves the complete standard schema to the output path.
   - On failure: log error, stay open, do **not** store path.
5. Log shows the output path. Dialog stays open; user reviews log.
6. User clicks **Close** → dialog exits with `Accept`.
7. `HdfReviewTab::onRegenerateMasks()` reads `dialog.savedHdf5Path()`;
   if non-empty calls `loadHdfFile(savedPath)` — full reload.

## HDF5 Schema

`saveMasksToHdf5()` (in `BatchMaskSources.cpp`) already writes a complete standard
schema identical to a live experiment:

- `/experiment_info` — timestamps, config attributes, ROI attributes, optional background dataset
- `/valid_frames/images`, `/valid_frames/masks`, `/valid_frames/metadata`
- `/invalid_frames/images`, `/invalid_frames/masks`, `/invalid_frames/metadata`

No changes to `BatchMaskSources.cpp` are required.

## What Is Removed

The entire **Output group box** and its widgets are removed from `BatchMaskDialog`:

| Removed widget | Member |
|----------------|--------|
| "Display results in review tab" checkbox | `displayCheck_` |
| "Save mask PNGs" checkbox | `savePngCheck_` |
| PNG directory edit + browse button | `pngDirEdit_`, `pngBrowseBtn_` |
| "Save masks to HDF5 file" checkbox | `saveHdf5Check_` |
| HDF5 path edit + browse button | `hdf5PathEdit_`, `hdf5BrowseBtn_` |

Associated slots `onBrowseOutputPng()`, `onBrowseOutputHdf5()`, and their signal
connections are also removed.

`saveMaskImages()` call in `onRun()` is removed (PNG saving gone).

`processedFrames()` getter and `results_` member remain — `processBatch()` still
populates them; they may be used by callers in future.

## New Members Added

```cpp
// BatchMaskDialog.h — public
QString savedHdf5Path() const { return savedHdf5Path_; }

// BatchMaskDialog.h — private
QString savedHdf5Path_;
QString computeAutoOutputPath() const;
```

`displayRequested()` is removed (no longer meaningful).

## Files Changed

| File | Change |
|------|--------|
| `include/frontend/dialogs/BatchMaskDialog.h` | Remove 7 output widget members + `displayRequested()`; add `savedHdf5Path_`, getter, `computeAutoOutputPath()` |
| `src/frontend/dialogs/BatchMaskDialog.cpp` | Remove output group box from `buildUi()`; remove output slots; simplify `onRun()`; add `computeAutoOutputPath()` |
| `src/frontend/tabs/HdfReviewTab.cpp` | Update `onRegenerateMasks()`: call `loadHdfFile(dialog.savedHdf5Path())` instead of `setRegeneratedFrames()` |

No new files required.

## Verification

1. Build Debug — zero errors.
2. Open an HDF5 file in HdfReviewTab → click "Regenerate masks…".
   - Dialog has no Output group box; only source, preview, config, progress, log.
   - Click Run — confirm auto-path logged (e.g. `experiment_001_remasked.h5`).
   - Click Close → HdfReviewTab reloads; scatter plot, histogram, metadata table,
     thumbnails all populated from the new file.
3. Run again with same source — confirm overwrite prompt appears.
   - Decline → run cancelled, no file written.
   - Accept → file overwritten, HdfReviewTab reloads.
4. Run with folder source — confirm output appears inside the folder.
5. Confirm `_remasked.h5` opens correctly in HdfReviewTab via File → Open.
