## Fix: HDF Review crash when loading initial thumbnails

- Context: Opening large HDF5 files in the Review tab sometimes crashed right after “Mem before batch” log and before “Mem after batch”.
- Root cause: `QSpacerItem` member pointers (`validBottomSpacer_`/`invalidBottomSpacer_`) were deleted when clearing the grids, but the member pointers were not nulled. Later spacer “replace” logic attempted to remove/delete them again, causing a double-delete.
- Impact: Crash during `updateImageGrid`/`loadThumbnailsBatch` on first load, especially with many frames.

### Reproduction
1. Launch `mock_studio_qt.exe`.
2. Open a large HDF5 file in Review.
3. Observe logs show “Mem before batch …” and application crashes shortly after.

### Resolution
- After clearing each grid, set the corresponding spacer pointer to `nullptr`.
- Replace spacer update blocks to “create-or-replace safely”:
  - If a spacer exists, remove it from the layout and delete it.
  - Always allocate a new spacer and add at the correct row.

Files edited:
- `src/frontend/HdfReviewTab.cpp`
  - `clearDisplay()`: null the spacer pointers after layout clears
  - `updateImageGrid()`: null spacer after grid clear; safe create/replace for spacer
  - `loadThumbnailsBatch()`: safe create/replace for spacer

### Verification
- Rebuild succeeded (`Release`). 
- On load, spacer handling is deterministic; no stale pointers remain after clears.
- Expect logs to show both “Mem before batch …” and “Mem after batch …” without crashing.

### Follow-ups
- None required; change is localized to UI spacer lifecycle.


