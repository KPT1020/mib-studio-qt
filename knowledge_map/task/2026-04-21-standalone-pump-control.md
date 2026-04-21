# 2026-04-21 — Standalone pump_control and pump modularization

## Summary

Extracted syringe-pump control into a standalone executable (`pump_control`) and
refactored the existing pump implementation to be portable and modular without
changing existing `mib_studio_qt` behavior for the two default pumps.

## Scope

- Added a slim backend library (`pump_backend`) with only:
  - `Tools` (now with cross-platform serial-port name enumeration)
  - `Logger`
  - `SyringePumpService`
- Added new executable target:
  - `pump_control` (Qt Core/Gui/Widgets/SerialPort + spdlog + nlohmann_json)
  - no OpenCV/HDF5/ONNX/SQLite linkage.
- Kept `mib_studio_qt` target behavior and dependencies unchanged.

## Backend refactor

### SyringePumpService

- Replaced fixed two-pump enum model with dynamic handles:
  - `PumpHandle addPump/removePump/clearPumps/pumpHandles/pumpCount`
  - all control APIs now accept `PumpHandle`.
- Added portable connect API:
  - `connect(PumpHandle, const QString& portName, int baud, uint8_t addr)`
  - Windows-only compatibility wrappers retained:
    - `connect(PumpHandle, int comPort, ...)`
    - `getComPort(PumpHandle)`
- Pump config now carries:
  - name, `portName`, baud, address
  - flow settings, direction
  - syringe volume + unit.

### Tools

- Added `availableSerialPortNames()` via `QSerialPortInfo` for cross-platform
  port discovery.
- Kept `availableComPortNumbers()` as Windows-oriented compatibility wrapper
  over names.

## Frontend refactor

### PumpRowWidget (new)

- New reusable row widget for one pump:
  - name edit
  - connect/disconnect
  - flow rate/unit + direction
  - start/stop/purge
  - status/readouts
  - per-row remove button
- Includes debounced apply signaling.

### SyringePumpTab

- Constructor decoupled from `AppBackend`:
  - takes `SyringePumpService&`
  - takes callback for reserved port names.
- UI now dynamic list of `PumpRowWidget` rows with Add/Remove controls.
- Persists pump array under `config["pump_ports"]`.
- Legacy key migration retained:
  - `pump_sample_*`, `pump_sheath_*` auto-migrate into pump array.
- `mib_studio_qt` keeps two default pumps (`Sample`, `Sheath`) through config
  migration behavior.
- standalone `pump_control` defaults to one pump if no config exists.

### SyringePumpSettingsDialog

- Constructor decoupled from `AppBackend`:
  - takes `SyringePumpService&`
  - takes reserved-port callback.
- Uses cross-platform serial names in UI combos.
- Still edits first two logical pumps (Sample/Sheath compatibility behavior),
  and applies connected syringe-volume changes live.
- Migrates legacy config keys to `pump_ports` format.

## Standalone app

- Added `PumpControlMainWindow`:
  - owns `SyringePumpService` by value
  - initializes default pump
  - central widget = `SyringePumpTab`
  - menu:
    - File → Exit
    - Settings → Pump Settings...
- Added `src/standalone/pump_control/main.cpp`:
  - QApplication identity = "Pump Control"
  - Windows debug console in `_WIN32 && _DEBUG`
  - crash log path:
    - Windows: `%LOCALAPPDATA%/Pump_Control/crash_log.txt`
    - others: `QStandardPaths::AppDataLocation/crash_log.txt`
  - logger initialized in writable app-data `logs/`.

## Build/deploy changes

- Added `pump_control` to Windows deployment loop (windeployqt + runtime DLLs).
- Scoped Coremor DLL and YOLO model copy post-build steps to `mib_studio_qt`
  only.

## Validation

- `cmake --preset windows-default` succeeded.
- `cmake --build build --config Debug --target pump_control` succeeded.
- `cmake --build build --config Debug --target mib_studio_qt` succeeded.

## Notes

- Context7 MCP server was unavailable in this environment (no MCP servers
  registered), so Qt API usage followed existing in-repo patterns and standard
  Qt APIs directly.
