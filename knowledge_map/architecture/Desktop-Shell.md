# Desktop Shell (React + Tauri v2)

> The Phase 3 deliverable of epic #246: the React + Tauri v2 desktop app that
> replaces the Qt frontend. It drives the Qt-free C++ backend through the
> [[Rust-Bridge]] (`mib-bridge`, ADR 0003). First vertical slice: **mock camera
> end to end**.

**Source:** `desktop/` — `src/` (React + Vite + TS frontend),
`src-tauri/` (Tauri v2 Rust app), `scripts/xvfb-smoke.sh`
**Bridge:** [[Rust-Bridge]] · **Backend seam:** [[AppBackend]] via
`backend::bridge::BackendFacade`
**CI:** `.github/workflows/desktop-ci.yml`
**Related:** [[../build-and-run/Dependencies]], [[Data-Flow]]

## Layout

The repo root `src/` is the C++ tree, so the whole Tauri app lives under
`desktop/`:

- `desktop/src/` — React frontend. `bridge.ts` is the typed IPC client
  (mirrors the `src-tauri` command DTOs); `App.tsx` is the mock-camera slice UI
  (init → configure mock dir → start/stop → live canvas + event log).
- `desktop/src-tauri/` — the Tauri v2 app. `src/lib.rs` holds `AppState`
  (`Mutex<UniquePtr<BackendBridge>>` + a cached last-frame buffer) and the
  `#[tauri::command]` layer; `main.rs` calls `run()`.
- `desktop/scripts/xvfb-smoke.sh` — headless GUI smoke launcher.

## Command layer

Thin wrappers over the bridge (all take the managed `AppState`):

- **Live capture:** `abi_version`, `is_initialized`, `init(data_dir)`,
  `configure_mock(dir, interval_ms, loop)`, `start_capture`, `stop_capture`,
  `seek_latest`, `poll_events` (→ serde `EventDto[]`), `fetch_frame`
  (→ `FrameMeta`, caches the pixel bytes), `frame_bytes` (→ `tauri::ipc::Response`
  — **raw Mono8 bytes as a binary IPC response, never base64**, per ADR 0003).
- **Recording + review (Phase 4 slice 1):** `start_recording(path)`,
  `stop_recording`, `load_recording(path)`, `seek_index(i)`,
  `fetch_frame_by_index(i)`.
- **Processing (Phase 4 slice 2):** `apply_processing(realtime, px→µm)` and
  `fetch_processing_stats` (fps + scale, polled each tick for a live overlay).

The frontend calls `fetch_frame` (or `fetch_frame_by_index`) then `frame_bytes`
for the same pull, and expands Mono8→RGBA on a canvas via `mono8ToImageData`
(honouring row stride). `PlaybackPosition` events (drained from `poll_events`)
bound the review scrubber.

## Build & run

- Frontend: `npm install && npm run build` in `desktop/` → `desktop/dist`
  (Tauri's `frontendDist`). `tsc` typechecks under strict mode.
- App: `cargo build` in `desktop/src-tauri` (needs `dist/` to exist — Tauri
  validates `frontendDist` at compile time). Links the bridge via
  `MIB_BRIDGE_NO_CMAKE=1` when the archives are prebuilt.
- Crate-type is **`rlib`** only (binary + rlib, an executable). The mobile
  `cdylib`/`staticlib` types are omitted because the non-PIC C++ archives can't
  link into a shared object — see [[Rust-Bridge]].

## Headless verification

Two gates, both without a real display:

1. `cargo test` — `mock_camera_slice_round_trip` drives init → configure → start
   → pull-frame (asserts 512×96) → stop → shutdown directly on the bridge from
   the desktop crate; `record_and_review_round_trip` drives record → load → seek
   by index → pull-by-index; `processing_settings_round_trip` drives
   apply-processing → pull-stats (asserts the px→µm scale round-trips); plus
   `event_kind_names_are_stable`.
2. `xvfb-smoke.sh` — launches the real binary under Xvfb with the container
   WebKitGTK workarounds (`WEBKIT_DISABLE_DMABUF_RENDERER=1`,
   `WEBKIT_DISABLE_COMPOSITING_MODE=1`, `LIBGL_ALWAYS_SOFTWARE=1`) and asserts
   the window + webview come up and stay alive. This is what makes the GUI
   verifiable in headless CI (`desktop-ci.yml`).

## Gotchas

- WebKitGTK needs the dmabuf/compositing env workarounds to initialize in a
  container; without them GTK init fails headless. `xvfb-smoke.sh` sets them.
- `dist/` must exist before `cargo build` — build the frontend first (CI does).
- Keep frame pixels on the `frame_bytes` binary channel; do not JSON/base64
  them through `poll_events` or a command return (ADR 0003 hot-path rule).
