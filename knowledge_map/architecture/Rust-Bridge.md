# Rust ↔ C++ Bridge

> The Phase 2 seam of the React + Tauri migration (epic #246): a `cxx` bridge
> that lets a Rust shell drive the Qt-free C++ backend with no Qt, no webkit,
> and no display. Wraps [[AppBackend]] via `backend::bridge::BackendFacade`.

**Source:** `crates/mib-bridge/` (`src/lib.rs`, `src/shim.h`, `src/shim.cpp`,
`build.rs`, `tests/contract.rs`)
**Backend seam:** `include/backend/app/BackendFacade.h`,
`src/backend/app/BackendFacade.cpp`
**Decision:** [`docs/decisions/0003-rust-cxx-bridge.md`](../../docs/decisions/0003-rust-cxx-bridge.md)
**Related:** [[AppBackend]], [[Threading-Model]], [[Data-Flow]],
[[../build-and-run/Dependencies]]

## Why cxx

Safe-by-construction FFI: ownership/lifetimes are expressed in the
`#[cxx::bridge]` module and checked at compile time on both sides, versus
hand-rolled `unsafe extern "C"` marshalling. A PoC proved it links our static
`mib_backend`/`mib_processing` archives and calls in headless before any
production code was written. `autocxx`/`bindgen` are not adopted (would be a new
ADR).

## Shape

Rust owns an opaque `BackendBridge` (`UniquePtr`) that composes an `AppBackend`
+ a `BackendFacade`. Rust never sees `AppBackend`, OpenCV, or HDF5 — only:

- **Lifecycle:** `new_backend_bridge()`, `initialize(data_dir)`, `shutdown()`,
  `is_initialized()`. Dropping the `UniquePtr` calls `shutdown()` then destroys
  the backend.
- **Commands (flat submitters over `BackendFacade::dispatch`):**
  `configure_mock_camera`, `start_capture`, `stop_capture`,
  `start_frame_recording`, `stop_frame_recording`, `playback_seek_latest`. Each
  returns a flattened `BridgeCommandResult { ok, command, message }`. No
  exceptions cross the boundary — the shim catches any C++ exception and returns
  `ok=false`.
- **Events (poll-drained queue):** `poll_events() -> Vec<BridgeEvent>`. The
  facade emits `BackendEvent`s from **backend threads** (the background-capture
  callback runs on the capture/processing thread). The shim's event sink does
  the minimum on that thread — serialise to a typed-slot `BridgeEvent` and push
  onto a mutex-guarded queue — then returns; Rust drains it. This is the
  non-blocking-sink rule from ADR 0003.
- **Frame pull:** `fetch_latest_frame() -> BridgeFrame` (metadata + one owned
  byte copy out of the playback store). Live-capture frames are **pulled on
  demand, never pushed** through the event channel and **never base64-encoded
  per frame** (epic principle #4 / ADR 0003 hot-path rule).

### `BridgeEvent` typed slots

Rather than mirror every `BackendEvent` variant field, events flatten into a
small pool of typed slots (`u0..u5`, `f0..f2`, `b0..b1`, `text`) whose meaning
depends on `kind` (`FrameReady` / `CameraStatus` / `RecordingStatus` /
`ProcessingResult` / `PlaybackPosition` / `BackendError`). The per-kind mapping
is documented in `event_to_bridge` in `shim.cpp` and is part of the versioned
contract — new fields append slots, never repurpose them.

## Versioned contract + test

The command/event set is a versioned schema: `bridge_abi_version()` returns `1`;
additive changes bump it. `tests/contract.rs` is the Phase 2 gate and boundary
regression guard — it drives a real
init → configure mock camera → start → poll `fetch_latest_frame` (asserts the
512×96 sample dims and `stride×height` byte count) → `playback_seek_latest` →
observe a `FrameReady` event → stop → shutdown lifecycle against the linked
backend, headless.

## Build

`build.rs` drives the `linux-backend-only` CMake preset to produce
`libmib_backend.a` / `libmib_processing.a`, then `cxx_build` compiles the bridge
+ `shim.cpp` and links the archives plus their system deps (OpenCV / HDF5 /
SQLite / spdlog / fmt / crypto). Set `MIB_BRIDGE_NO_CMAKE=1` to skip the cmake
step when the caller already built the archives (the CI lane does this). HDF5
shared libs need `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/hdf5/serial` at
runtime on Ubuntu.

CI: `.github/workflows/bridge-ci.yml` builds the archives then runs
`cargo test` — no Qt, no webkit, no display.

## Gotchas

- The bridge links the **static** archives, so it depends on them being built
  first; `build.rs` handles that unless `MIB_BRIDGE_NO_CMAKE=1`.
- `FrameReady` events fire from the **background-capture / playback** paths, not
  from plain live capture — plain live frames flow into the playback store and
  are read via `fetch_latest_frame`. To emit a `FrameReady` for the latest live
  frame, dispatch `playback_seek_latest` (the push path the webview subscribes
  to). This mirrors `tests/backend/backend_facade_boundary_test.cpp`.
- Keep the event sink non-blocking on the C++ thread; do not add a per-frame
  base64/JSON pixel channel — extend the command/event variants (with a version
  bump) instead.
