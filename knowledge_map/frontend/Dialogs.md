# Dialogs

> Short-lived modal widgets. One note for all — each is small.

**Source:** `src/frontend/dialogs/`, `include/frontend/dialogs/`

| Dialog | Purpose | Surfaced by |
|---|---|---|
| `MockConfigDialog` | Pick mock camera folder, interval, loop | [[ConnectTab]] |
| `ProcessingSettingsDialog` | Edit `ProcessingConfig` (full form) | [[ConfigTabs]] / menu |
| `MonitoringSettingsDialog` | Chart bin counts, axis ranges, refresh rate | [[ExperimentMonitoringTab]] |
| `BufferSaveDialog` | Save FrameStore frames to TIFF (range selection) | [[PreviewPage]] |
| `ConversionFactorDialog` | Set pixel→μm conversion factor | [[PreviewPage]] |
| `FrameViewerDialog` | Popout frame inspector with overlay toggles | [[HdfReviewTab]], [[PreviewPage]] |
| `SyringePumpSettingsDialog` | Per-pump COM port, baud, Modbus address | [[SyringePumpTab]] |

## Conventions

- Each dialog takes a non-owning reference to the relevant service or
  config struct and emits/returns an updated value on `accept()`.
- Dialogs do **not** spawn threads or open serial ports themselves — they
  only mutate config; opening happens in the owning tab.
