Title: Review system scalable to 2GB (lazy reads, virtualization)

Date: 2025-11-18

Context
- Opening large HDF5 files first caused crashes due to eager full-dataset loads (images+masks) and dense UI tables.
- Goal: Make Review tab scalable for files up to ~2GB by avoiding unbounded allocations.

Changes
- Backend: Added scalable read APIs to `Hdf5Service`:
  - `getDatasetInfo`, `readImageByIndex`, `readImagesRange`, `readValidMetadata`, `readInvalidMetadata`.
  - Uses HDF5 hyperslabs; bounds memory; logs with spdlog.
- Frontend (`HdfReviewTab`):
  - Keeps HDF5 reader open during review; reads only metadata at file load.
  - Lazy thumbnail loading on scroll; small LRU cache (default 2048 items).
  - Frame viewer fetches image/mask on demand for current index.
  - Metrics table replaced with `QTableView + HdfMetricsModel` (virtualized).

Behavior
- Large files open without loading all images; memory rises with visible thumbnails only.
- Double-click opens a viewer that fetches the current frame lazily.
- ROI/processing overlays re-render thumbnails and viewer images on demand.

Notes
- Backward compatible on-disk format.
- Chunked datasets recommended; optional thumbnails dataset may further reduce I/O later.

Verification
- Open large HDF5 file first; UI remains responsive and memory steady.
- Scroll grid: thumbnails load in batches; logs show batch loads.
- Viewer navigation fetches frames without spikes.







