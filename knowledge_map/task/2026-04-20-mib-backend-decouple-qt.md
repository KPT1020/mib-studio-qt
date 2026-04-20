# mib_backend decoupled from Qt (2026-04-20)

Canonical architecture note: [[../architecture/Tauri-Bridge]].

## Summary

- `mib_backend` no longer links Qt6. Syringe pump Modbus RTU uses Win32 `CreateFile`/`ReadFile`/`WriteFile`/`SetCommState` (planned `libserialport` Conan dep was dropped: recipe not on ConanCenter; Win32 avoids extra dependencies).
- `BackgroundCaptureNotifier` (`QObject`) removed. `AppBackend::setBackgroundCaptureCallback(std::function<void(const cv::Mat&, uint64_t)>)` replaces it. Qt UI uses `frontend::BackgroundCaptureAdapter`; Tauri bridge sets the same callback and emits `background:captured` with PNG bytes.
- `MockCamera` loads frames via OpenCV `cv::imread` only (no `QImageReader`).
- `src-tauri/build.rs` skips Conan `qt6*` / `qt-*-release-x86_64-data.cmake` harvest and panics if any harvested lib starts with `Qt6`.
- Conan: `qt/*:qtserialport=False` (backend no longer needs Qt SerialPort).

## Verification

- `cmake --build build --config Release --target mib_backend`
- `cmake --build build --config Release --target mib_studio_qt`
- `cargo build --release` in `src-tauri/`

## Runtime

- Tauri `mib-studio.exe` should start without Conan Qt on `PATH` (no silent `0xC0000135` from missing `Qt6Core.dll`). Non-Qt deps (OpenCV, HDF5, spdlog, etc.) must still be discoverable (copy next to exe or extend `PATH`).
