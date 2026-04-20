# Tauri backend bridge

The Tauri shell (`src-tauri/`) links the static `mib_backend` library and a small cxx bridge (`src-tauri/src/bridge/`) that talks to `backend::AppBackend`.

## Build order

1. Configure and build CMake Release `mib_backend` (and Conan deps) so `build/Release/mib_backend.lib` exists.
2. `cargo build` / `npm run tauri:dev` in `src-tauri` context — `build.rs` harvests `build/*-release-x86_64-data.cmake` for link libs and package `include/` paths.

If you change `CaptureService::FrameCallback` or other ABI used by the bridge, rebuild `mib_backend` before linking the Rust binary.

## Runtime

Set `PATH` so Conan Qt/OpenCV/HDF5 DLLs resolve (see Phase A notes). Mock camera: `MIB_CAMERA_MODE=mock` and optional `MIB_MOCK_*` vars as in `CLAUDE.md`.

## Events

- `frame:new` — base64 PNG preview (hot path; optimize later if needed).
- `stats:update` — capture + processing metrics every 500ms from the bridge stats thread.

Registering emitters runs in `main` setup after `AppState` is managed so `AppHandle` is available for `Emitter::emit`.
