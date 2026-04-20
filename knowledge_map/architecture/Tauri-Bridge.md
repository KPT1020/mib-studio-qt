# Tauri Bridge

> Second desktop shell: Tauri (Rust + web UI) using the same C++ `mib_backend`
> as the Qt app through a `cxx` bridge.

**Source:** `src-tauri/src/main.rs`, `src-tauri/src/bridge/{mod.rs,ffi.rs,bridge.cpp}`, `src-tauri/build.rs`
**Related:** [[AppBackend]], [[Data-Flow]], [[../build-and-run/Build]], [[../build-and-run/Run-Modes]]

## Responsibility

- Provides a Tauri desktop entry point (`mib-studio.exe`) that reuses
  `backend::AppBackend` instead of duplicating capture/processing logic.
- Converts selected backend results to frontend-friendly payloads:
  - pull APIs via Tauri commands (`invoke`)
  - push APIs via events (`frame:new`, `stats:update`, `background:captured`)
- Keeps Tauri runtime independent of Qt DLL loading by linking a Qt-free
  `mib_backend` and filtering Qt Conan libs during Rust build.

## Runtime flow

```mermaid
flowchart LR
  subgraph tauriLayer [TauriLayer]
    invokeCmds[TauriCommands]
    eventSink[TauriEventEmitter]
  end
  subgraph rustLayer [RustLayer]
    backendFacade[BackendFacade]
    ffiLayer[cxxFfi]
  end
  subgraph cppLayer [CppLayer]
    shim[AppBackendShim]
    appBackend[AppBackend]
  end
  invokeCmds --> backendFacade
  backendFacade --> ffiLayer
  ffiLayer --> shim
  shim --> appBackend
  shim -->|"mib_emit_* callbacks"| eventSink
```

## Current FFI surface (implemented)

Implemented in `ffi.rs` + `bridge.cpp` and consumed by `bridge/mod.rs`:

- Backend lifecycle:
  - `create_shim`
  - `backend_version`
- Camera:
  - discover cameras/framegrabbers
  - set hardware camera selection
  - configure mock camera
- Capture:
  - start, stop, running state
- Playback:
  - latest frame as PNG bytes
  - frame-by-index as PNG bytes
  - latest frame metadata
  - frame-by-index metadata
  - playback range
- Processing:
  - get/set processing config
  - set/clear realtime ROI
  - set realtime background from latest playback frame
  - monitoring valid/invalid frame snapshots (with base64 PNG + metrics)
- HDF5/experiment:
  - start/stop experiment lifecycle (open/init datasets, flush, metadata write, close)
  - load existing HDF5 file
  - read valid/invalid frames
  - export metrics CSV
  - start/stop frame recording
- Autofocus:
  - connect/disconnect
  - enable/disable
  - manual voltage step up/down
  - get/set autofocus config
- Syringe pump:
  - connect/disconnect
  - set flow rate
  - set direction
  - start/stop/purge
  - get status/config
- Trigger/config:
  - fire sort trigger and set pulse duration
  - get/set app config JSON
  - apply camera script
  - set pixel-to-micron factor
  - save playback buffer to disk
- Emitters:
  - install callbacks for frame stream, periodic stats, background capture

## Command coverage

`src-tauri/src/main.rs` registers many command modules. Current coverage:

- **Wired to backend bridge now**
  - `commands/camera.rs`
  - `commands/capture.rs`
  - `commands/playback.rs`
  - `commands/processing.rs`
  - `commands/hdf5.rs`
  - `commands/autofocus.rs`
  - `commands/syringe_pump.rs`
  - `commands/trigger.rs`
  - `commands/config.rs`

## Build and link model

1. Build C++ backend first (Release): `mib_backend.lib`.
2. Build Tauri Rust crate (`cargo build --release` in `src-tauri/`).
3. `src-tauri/build.rs`:
   - compiles `src/bridge/ffi.rs` + `src/bridge/bridge.cpp` with `cxx_build`
   - links `mib_backend.lib` and transitive Conan/vcpkg-resolved native libs
   - skips `qt6*` / `qt-*` Conan data files and panics if `Qt6` libs leak into
     link inputs

This keeps Tauri runtime free from Conan Qt DLL requirements while still
requiring other native dependencies (OpenCV, HDF5, Coremor, EGrabber-side DLLs
as applicable) to be deployable beside the executable or on `PATH`.

## Platform dependency gating (Cargo)

- `src-tauri/Cargo.toml` now makes Tauri desktop crates Windows-only:
  - `tauri`
  - `tauri-plugin-dialog`
  - `tauri-plugin-fs`
- `src-tauri/src/main.rs` gates Tauri runtime modules under `#[cfg(windows)]`
  and provides a non-Windows fallback `main()` so Linux/cloud `cargo check`
  succeeds without GTK/WebKit system packages.
- `src-tauri/build.rs` also uses a non-Windows early-return path and skips
  native bridge linking outside Windows.

## Gotchas

- Rebuild `mib_backend` after changing capture callback/FFI signatures; stale
  symbols can break Rust linking.
- `src/bridge/bridge.cpp` at repo root is an older stub path; the active Tauri
  implementation is `src-tauri/src/bridge/bridge.cpp`.

## History notes

- [[../task/tauri-phase-b-vertical-slice]]
- [[../task/2026-04-20-mib-backend-decouple-qt]]
