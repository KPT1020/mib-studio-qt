# Mock Camera Dev Mode

- Adds camera abstraction (`camera/common`) so `CaptureService` can stream from either the EGrabber hardware path or a folder-backed mock camera.
- `AppBackend` reads env vars (`MIB_CAMERA_MODE`, `MIB_MOCK_CAMERA_DIR`, `MIB_MOCK_CAMERA_INTERVAL_MS`, `MIB_MOCK_CAMERA_LOOP`) to decide which implementation to inject; `configureMockCamera` lets new tooling override this at runtime.
- `MockCamera` uses `QImageReader` to load `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif/.tiff` images, converts them to PFNC Mono8, and mirrors the stats interface used by EGrabber.
- Performance: mock now preloads frames into an in-memory cache and serves from memory to achieve ≥5k fps; per-frame logs removed in favor of periodic capture stats.
- Mock camera can be configured via the ConnectTab UI (using `MockConfigDialog`) or via environment variables before launching `mib_studio_qt.exe`.
- Docs: see `docs/howto/mock-camera-dev-mode.md` for setup instructions.

