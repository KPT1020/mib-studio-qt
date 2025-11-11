# Mock Camera Dev Mode

We support running the Qt frontend without camera hardware by swapping the capture pipeline over to a folder-backed mock camera. The mock reuses the same `CaptureService` interface used by the EGrabber implementation, so the frontend and processing services behave exactly as if frames were streaming from the device.

## Quick start: mock preview app

Run `mock_studio_qt.exe` (Debug or Release). On launch it shows a dialog where you can:

- choose a folder containing frame images (`.tiff`, `.png`, `.jpeg`, etc.)
- set the target frame rate (fps), which drives the mock frame interval (supports sub-millisecond resolution for >=1000 fps)

After you confirm, the regular studio window opens and you can start capture without any environment variables. The app remembers nothing between runs, so just relaunch to pick a different folder or fps.

## Enabling the mock (legacy env flow)

Set the following environment variables before launching either `mib_studio_qt` or the console test harness:

- `MIB_CAMERA_MODE=mock` &mdash; selects the folder-backed camera. Any other value (or unset) keeps the hardware path.
- `MIB_MOCK_CAMERA_DIR=<absolute-or-relative-path>` &mdash; directory containing the images to stream. Defaults to `<data>/mock_frames`.
- `MIB_MOCK_CAMERA_INTERVAL_MS` (optional) &mdash; delay between frames in milliseconds, default `33` (~30 fps). For finer control use the mock config dialog or set options programmatically (microsecond precision).
- `MIB_MOCK_CAMERA_LOOP` (optional) &mdash; set to `false`, `0`, or `no` to stop after the last frame. Otherwise the sequence loops.

The console test `capture_processing_test` still seeds `data/mock_frames/frame_000.png` automatically and forces `MIB_CAMERA_MODE=mock`, so it can be used to sanity-check the mock flow.

## Image requirements

The mock loads files with `QImageReader`, so any Qt-supported format works: `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, `.tiff`, etc. Each image is converted to PFNC `Mono8` (`0x01080001`) before being forwarded through `CaptureService`, matching the grayscale payload produced by our EGrabber path.

Images are streamed in lexical order by filename. For deterministic playback, use a numeric prefix (e.g. `frame_0001.png`).

## Stats and logging

`MockCamera` preloads all images into memory on start and serves frames from an in-memory cache to maximize throughput. It tracks the measured frame rate and data rate based on delivery timestamps, exposing them through `CaptureService::stats()` like the hardware camera. Logging (via `spdlog`) reports the source folder, microsecond interval, and derived fps during backend initialization, and warns when images are missing or unreadable. There is no per‑frame logging; rely on the periodic capture stats instead.

## High-fps sanity check

To confirm 5000 fps playback:

1. Launch `mock_studio_qt.exe`, select your frame folder, and dial the frame rate to `5000`.
2. Start capture and let it run for a few seconds. The backend log prints `interval=200 us (~5000.0 fps)` for the mock camera.
3. Open the capture stats panel (or tail the log) — `CaptureService` should report a frame rate close to 5000 fps once steady.
4. Pause playback; frames remain buffered at the configured rate, so you can scrub without waiting for rendering.

## References

- Hardware path mirrors the Euresys SDK high frame rate samples (e.g. `egrabber-sample-programs/python/310-high-frame-rate.py`).
- Mock implementation lives in `camera/mock/MockCamera` and is injected via `CaptureService::setCameraFactory`.


