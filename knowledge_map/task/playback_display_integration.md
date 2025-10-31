# Playback and Display Integration

- Scope: In-memory playback path integrated into Qt app using Euresys SDK high frame rate pattern.
- References: egrabber-snippets 310/311/312, 260/261 for recorder.

## Components
- backend::playback::FrameStore: thread-safe ring; copies each delivered part (image) with metadata.
- backend::services::CaptureService: pushes image parts into FrameStore while still invoking existing frame callback.
- backend::services::PlaybackService: minimal façade to fetch latest frame for display.
- frontend::PlaybackPanel: Qt widget polling ~30 FPS and rendering frames as Grayscale8.

## Notes
- Pixel format currently treated as Mono8 for display; extend conversion if needed.
- Line pitch is taken from BUFFER_INFO_CUSTOM_LINE_PITCH.
- Stats remain from StreamModule; display is decoupled from capture loop.

## Future
- Add disk-backed playback via Recorder (260/261) when persistence is needed.
- Handle color conversions (e.g., Bayer to RGB) if camera output is not Mono8.

## Scrubber (Review) Workflow
- FrameStore now exposes absolute index helpers: `earliestAvailableIndex()`, `latestAvailableIndex()`, and `availableCount()`.
- PlaybackService exposes `queryRange(earliest, latest, count)` and `fetchByIndex(idx, frame)` in addition to `fetchLatest`.
- PlaybackPanel hosts an image canvas and a horizontal `QSlider`:
  - When the user is not scrubbing, the slider auto-follows the latest frame and the view shows live images.
  - While scrubbing (slider pressed), the view shows the frame at the slider's absolute index.
  - Slider range dynamically reflects the current in-memory window `[earliest, latest]`.