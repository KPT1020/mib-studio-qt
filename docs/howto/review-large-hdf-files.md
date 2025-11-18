# Review large HDF5 files (2GB-ready)

This guide explains how the Review tab handles large experiment files efficiently.

## What changed
- The app no longer loads all images/masks at open time.
- Only metadata is read eagerly; images are fetched lazily in small batches using HDF5 hyperslabs.
- The metrics table is virtualized (`QTableView`) to avoid per-cell widget overhead.

## Usage
1. Open the Review tab and select an `.h5` / `.hdf5` file.
2. Thumbnails appear as you scroll; the app loads batches incrementally.
3. Double-click a thumbnail or a table row to open the viewer. Navigation keys (←/→) fetch the next/previous frame on demand.
4. Toggle overlays to view processing/ROI; thumbnails and viewer update as needed.

## Performance notes
- Thumbnail cache size defaults to 2048 entries; it can be tuned in code (`HdfReviewTab`). Logs show cache activity and batch loads.
- File stays open while you review to reduce open/close overhead.
- The underlying HDF5 dataset format is unchanged; chunked, unlimited-first-dimension datasets are recommended for fast random access.

### Virtualization details
- The thumbnails grid uses a bottom spacer to represent off-screen rows, avoiding thousands of placeholder widgets.
- Thumbnails are loaded in an initial batch, then in small batches on scroll.
- Memory diagnostics are logged around each batch (Windows): “Mem before batch …” and “Mem after batch …”.

## Troubleshooting
- If thumbnails are slow, verify datasets are chunked and on SSD/NVMe storage.
- If memory usage seems high, reduce the thumbnail cache size.


