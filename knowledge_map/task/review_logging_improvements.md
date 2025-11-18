Title: Improve Review logging to diagnose crash on large-first open

Date: 2025-11-18

Changes
- Backend `Hdf5Service`:
  - Added DEBUG/TRACE logs in `getDatasetInfo`, `readImageByIndex`, `readImagesRange` with dataset path, shapes, indices, and sizes.
- Frontend `HdfReviewTab`:
  - Logs at file open, dataset infos, grid updates, batch loads, cache usage, and viewer fetches.
  - Overlay toggles clear cache and rebuild with logs.

Usage
- Run app with logging enabled (default). Inspect `data/logs/app.log` for DEBUG/TRACE lines to identify the point of failure when opening large files first.





