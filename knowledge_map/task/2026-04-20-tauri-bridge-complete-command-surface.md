# Tauri bridge complete command surface (2026-04-20)

Canonical architecture note: [[../architecture/Tauri-Bridge]].

## Scope

- Completed the Tauri `cxx` bridge surface so every command module registered in
  `src-tauri/src/main.rs` is backed by real `backend::AppBackend` calls.
- Added missing FFI PODs and bridge functions for:
  - playback by index
  - processing config/ROI/background/monitoring snapshots
  - experiment lifecycle + HDF5 reads + CSV export + frame recording
  - autofocus control/config
  - syringe pump control/status/config
  - trigger controls
  - app config/script/buffer-save operations
- Replaced command-layer placeholders in
  `commands/{processing,hdf5,autofocus,syringe_pump,trigger,config}.rs` with
  calls to `Backend` methods in `bridge/mod.rs`.

## Behavioral notes

- Processing config mapping preserves ring-ratio fields by reading current config
  first and only replacing frontend-exposed fields.
- `set_realtime_background` now uses latest playback frame and converts to
  grayscale when needed before `setRealtimeBackgroundGray()`.
- HDF5 experiment stop follows the existing Qt flow:
  flush buffered frames, append remaining frames, flush file, write experiment
  metadata/config JSON, close file, then end experiment + reset realtime metrics.
- `export_metrics_csv` can work against a provided file path (loads then closes)
  or an already-open HDF5 handle.

## Validation in cloud

- `npm run build` passed for the React/Tauri frontend package.
- `cargo check` remains blocked in this Linux cloud image due to missing GTK3
  system dev packages (`gdk-3.0.pc` not found for `gdk-sys`), after upgrading
  Rust toolchain to stable `1.95.0`.

