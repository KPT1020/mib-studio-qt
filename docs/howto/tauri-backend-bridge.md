# Tauri backend bridge

The Tauri shell (`src-tauri/`) links the static `mib_backend` library and a small cxx bridge (`src-tauri/src/bridge/`) that talks to `backend::AppBackend`.

## Build order

1. Configure and build CMake Release `mib_backend` (and Conan deps) so `build/Release/mib_backend.lib` exists.
2. `cargo build` / `npm run tauri:dev` in `src-tauri` context — `build.rs` harvests `build/*-release-x86_64-data.cmake` for link libs and package `include/` paths.

If you change `CaptureService::FrameCallback` or other ABI used by the bridge, rebuild `mib_backend` before linking the Rust binary.

## Runtime

`src-tauri/build.rs` copies **Conan package `bin\\*.dll`** (from `*-release-x86_64-data.cmake` package folders, excluding Qt) and **`include/Coremor/XMT_DLL_SER.dll`** into `src-tauri/target/<profile>/` next to `mib-studio.exe`, so **`cargo run`** usually does not require a special `PATH`.

If anything is still missing and you get **`0xC0000135` (STATUS_DLL_NOT_FOUND)** (e.g. before the build script runs, or a new dependency), use Conan’s run environment: `conanfile.txt` includes **VirtualRunEnv** so `conan install . -of build ...` can generate `build/conanrun.bat`.

From repo root:

```text
cmd /c "call build\conanrun.bat && cd src-tauri && cargo run --release"
```

Or: `.\scripts\run-tauri-with-conan-path.ps1`

(`mib_backend` is Qt-free; you do not need Qt DLLs for the Tauri process.)

Mock camera: `MIB_CAMERA_MODE=mock` and optional `MIB_MOCK_*` vars as in `CLAUDE.md`.

## Mock camera folder + FPS workflow (React/Tauri)

Use the Connect tab's `Configure Mock...` flow to configure the mock source at runtime:

1. Pick a folder containing image frames.
2. Enter target FPS (1-1000).
3. Click `OK`, then start camera capture.

Validation rules:

- Folder must exist and be a directory.
- Folder must contain supported image files (`.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, `.tiff`).
- Every discovered image file must be readable by OpenCV.
- Frame interval must be at least 1 ms.

If validation fails, `configure_mock` returns an explicit error to the dialog instead of failing silently at capture start.

## Manual verification checklist

- Configure a valid image folder and FPS in `Configure Mock...`; confirm dialog closes and status updates.
- Start camera; verify frames advance in the playback canvas and frame index changes over time.
- Confirm `stats:update` capture FPS is close to configured target (allowing scheduler jitter).
- Stop camera; verify stream stops and status updates to stopped state.
- Try invalid inputs:
  - empty path,
  - non-existent folder,
  - folder with no supported files,
  - folder with unreadable/corrupt image.
  Confirm each path shows a clear error message.

## Events

- `frame:new` — base64 PNG preview (hot path; optimize later if needed).
- `stats:update` — capture + processing metrics every 500ms from the bridge stats thread.

Registering emitters runs in `main` setup after `AppState` is managed so `AppHandle` is available for `Emitter::emit`.
