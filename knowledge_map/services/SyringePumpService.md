# SyringePumpService

> Dual-pump control (Sample + Sheath) via Modbus RTU over serial.

**Source:** `src/backend/services/SyringePumpService.cpp`,
`include/backend/services/SyringePumpService.h`
**Related:** [[../frontend/SyringePumpTab]],
[[../frontend/Dialogs]] (SyringePumpSettingsDialog)

## Responsibility

- Maintain two independent `QSerialPort` connections
  (`PumpId::Sample`, `PumpId::Sheath`).
- Per-pump control: `setFlowRate`, `setDirection`, `start`, `stop`,
  `purge`, `stopPurge`, `setSyringeVolume`.
- Per-pump status polling: `pollStatus(id)` — UI timer drives this.
- Expose config/status structs (`PumpConfig`, `PumpStatus`).
- Provide `scanModbusAddresses(comPort, baudRate, start, end, timeoutMs)` for
  settings-time address discovery on a selected serial port.

## Enums

- `RunStatus`: Stop (0), Forward (1), Backward (2), Pause (3)
- `Direction`: Infuse (0), Withdraw (1)
- `flowRateUnit` uses integer codes (e.g. `100` = µL/min)

## Modbus helpers

Private: CRC-16, `buildReadRequest`, `buildWriteSingleRequest`,
`buildWriteMultipleRequest`, big-endian ABCD float ↔ two 16-bit registers,
`readHoldingRegisters`, `writeSingleRegister`,
`writeMultipleRegisters`.

The pure framing primitives live in `include/backend/services/ModbusRtu.h`
(`backend::services::modbus`), unit-tested by
`tests/backend/modbus_rtu_test.cpp`. As of the Qt-decoupling work (epic #246)
they are **Qt-free**: frames are `std::vector<uint8_t>` (`modbus::Frame`), not
`QByteArray`. `SyringePumpService` converts to/from `QByteArray` only at the
`QSerialPort` read/write seam inside the `.cpp`; `SyringePumpService.h` no
longer forward-declares or exposes `QByteArray`. Serial I/O itself still uses
`QSerialPort` — moving that behind a platform-neutral interface is a later
migration slice (see
[the Qt-decoupling exec-plan](../../docs/exec-plans/active/2026-07-15-qt-decoupling-and-tauri-migration.md)).

Public scan helper probes `REG_RUN_COMMAND` (`0x0001`) with Modbus function
`0x03` over an address range (default 1..8) and returns responsive addresses.

## Threading

Each pump has its own `std::mutex` guarding serial I/O. UI calls are
synchronous; `pollStatus` is invoked from the Qt timer in
[[../frontend/SyringePumpTab]].

## Gotchas

- `getComPort(id)` is used by [[../frontend/Dialogs]] SyringePumpSettingsDialog
  to avoid double-assigning a COM port to both pumps.
- Dialog-provided `baudRate` and `modbusAddress` must match the hardware.
- See `docs/dLSP_pump.pdf` for pump protocol reference (shipped in repo).
