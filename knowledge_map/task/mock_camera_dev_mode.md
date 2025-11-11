# Mock Camera Dev Mode

- Adds camera abstraction (`camera/common`) so `CaptureService` can stream from either the EGrabber hardware path or a folder-backed mock camera.
- `AppBackend` reads env vars (`MIB_CAMERA_MODE`, `MIB_MOCK_CAMERA_DIR`, `MIB_MOCK_CAMERA_INTERVAL_MS`, `MIB_MOCK_CAMERA_LOOP`) to decide which implementation to inject.
- `MockCamera` uses `QImageReader` to load `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif/.tiff` images, converts them to PFNC Mono8, and mirrors the stats interface used by EGrabber.
- `capture_processing_test` now forces mock mode and seeds a minimal PNG in `data/mock_frames` so CI/devs can exercise the display pipeline without hardware.
- Docs: see `docs/howto/mock-camera-dev-mode.md` for setup instructions.

