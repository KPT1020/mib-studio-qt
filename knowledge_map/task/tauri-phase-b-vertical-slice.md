# Tauri Phase B vertical slice (done)

Canonical architecture note: [[../architecture/Tauri-Bridge]].

## Scope

- cxx bridge: free functions `bridge_*` + shared POD structs matching `ffi.rs` (also declared in `bridge.h` with `CXXBRIDGE1_*` guards).
- `install_emitters_impl`: `processing().startRealtime(frameStore)`, `capture().setFrameCallback` (PNG via OpenCV `imencode`), 500ms stats thread -> `mib_emit_stats`.
- Rust: `OnceLock<AppHandle>`, `Emitter::emit` for `frame:new` / `stats:update` (payloads `camelCase` JSON).
- Commands wired: camera discover/connect/mock, capture start/stop/running, playback fetch_latest + range.
- `CaptureService::FrameCallback` extended with `line_pitch` + `pixel_format` for correct `cv::Mat` wrapping.

## Build notes

- After changing `CaptureService::FrameCallback`, rebuild `mib_backend` before `cargo build` or link fails on `setFrameCallback` symbol mismatch.
- `build.rs` adds Conan `PACKAGE_FOLDER_RELEASE/include` dirs so `bridge.cpp` finds OpenCV headers.
- `CameraControlTypes.h` splits discovery structs from `CameraControlService.h` so Tauri bridge compiles without `EGrabber.h` on the include path.

## Deferred

- `fetch_frame_by_index` (needs bridge API).
- Remaining command stubs (processing, HDF5, etc.).
