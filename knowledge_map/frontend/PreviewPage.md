# PreviewPage

> Central workspace widget: live preview canvas on the left, playback scrub
> and overlays in the top splitter pane, [[ConfigTabs]] in the bottom
> pane, and the [[RunDashboardStrip]] (UX-7) installed above the image
> via `setDashboardWidget()`.

**Source:** `src/frontend/tabs/PreviewPage.cpp`,
`include/frontend/tabs/PreviewPage.h`
**Related:** [[System-Utilities]] (`PlaybackPanel`), [[ConfigTabs]],
[[../data-model/FrameStore]], [[../services/ProcessingService]]
(realtime snapshot)

## Responsibility

- Host the `PlaybackPanel` widget (see [[System-Utilities]]) for live + scrub
  display with ROI drawing and overlay modes (mask, contours, both).
- Wire an `AppConfigWatcher` to pick up external JSON config changes
  (see `docs/howto/live-config-reload.md`).
- Bridge UI events (ROI changes, overlay toggles, zoom fit) to
  [[../services/ProcessingService]] (`setRealtimeRoi`,
  `setRealtimeBackgroundGray`) and to
  [[../frontend/System-Utilities]] (`PlaybackPanel`).

## UI controls

Overlay mode, fit mode (FitToWindow / Zoom100), display FPS target, save
buffer to disk (via [[Dialogs]] `BufferSaveDialog`), conversion-factor
spinner (μm/pixel — see [[Dialogs]] `ConversionFactorDialog`).

## Gotchas

- Display FPS is capped (60 Hz by default) — see task
  `knowledge_map/task/2025-11-17-preview-60hz.md`.
- PreviewPage does not push frames to HDF5; only [[../services/ProcessingService]]
  does, during an active experiment.
- Overlay cell color is per-frame only while following live; in paused/replay
  it is recomputed from the displayed buffered frame (see [[System-Utilities]]
  `PlaybackPanel`).
