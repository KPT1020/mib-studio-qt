# Mock Camera Dev Mode

We support running the Qt frontend without camera hardware by swapping the capture pipeline over to a folder-backed mock camera. The mock reuses the same `CaptureService` interface used by the EGrabber implementation, so the frontend and processing services behave exactly as if frames were streaming from the device.

## Enabling the mock

Set the following environment variables before launching either `mib_studio_qt` or the console test harness:

- `MIB_CAMERA_MODE=mock` &mdash; selects the folder-backed camera. Any other value (or unset) keeps the hardware path.
- `MIB_MOCK_CAMERA_DIR=<absolute-or-relative-path>` &mdash; directory containing the images to stream. Defaults to `<data>/mock_frames`.
- `MIB_MOCK_CAMERA_INTERVAL_MS` (optional) &mdash; delay between frames in milliseconds, default `33` (~30 fps).
- `MIB_MOCK_CAMERA_LOOP` (optional) &mdash; set to `false`, `0`, or `no` to stop after the last frame. Otherwise the sequence loops.

The console test `capture_processing_test` now seeds `data/mock_frames/frame_000.png` automatically and forces `MIB_CAMERA_MODE=mock`, so it can be used to sanity-check the mock flow.

## Image requirements

The mock loads files with `QImageReader`, so any Qt-supported format works: `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, `.tiff`, etc. Each image is converted to PFNC `Mono8` (`0x01080001`) before being forwarded through `CaptureService`, matching the grayscale payload produced by our EGrabber path.

Images are streamed in lexical order by filename. For deterministic playback, use a numeric prefix (e.g. `frame_0001.png`).

## Stats and logging

`MockCamera` tracks a synthetic frame rate and data rate based on the configured interval, exposing them through `CaptureService::stats()` like the hardware camera. Logging (via `spdlog`) reports the source folder and interval during backend initialization, and warns when images are missing or unreadable.

## References

- Hardware path mirrors the Euresys SDK sample `egrabber-sample-programs/cpp/egrabber-snippets/samples/310-high-frame-rate.cpp`.
- Mock implementation lives in `camera/mock/MockCamera` and is injected via `CaptureService::setCameraFactory`.


