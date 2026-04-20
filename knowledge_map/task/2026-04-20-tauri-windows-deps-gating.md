# Tauri Windows-only dependency gating for cloud checks (2026-04-20)

Canonical architecture note: [[../architecture/Tauri-Bridge]].

## Scope

- Fixed Linux/cloud dependency failures for `src-tauri` by gating Tauri desktop
  dependencies to Windows only in `src-tauri/Cargo.toml`:
  - `tauri`
  - `tauri-plugin-dialog`
  - `tauri-plugin-fs`
- Gated Tauri runtime entrypoints in `src-tauri/src/main.rs` with `#[cfg(windows)]`,
  and provided a non-Windows fallback `main()` that exits with an informative
  message.
- Adjusted `src-tauri/build.rs` to split Windows/non-Windows paths:
  - non-Windows: skip native bridge/link steps and emit a warning
  - Windows: retain full `tauri_build` + C++ bridge linking behavior

## Result

- `cargo check` now passes in Linux cloud images without requiring GTK/WebKit
  apt packages for Tauri desktop runtime.
- Windows behavior remains the production path for Tauri app runtime and native
  bridge linking.

## Validation

- `cargo check` (passes on Linux cloud after dependency gating)
- `npm run build` (passes)
