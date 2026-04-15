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

## Enums

- `RunStatus`: Stop (0), Forward (1), Backward (2), Pause (3)
- `Direction`: Infuse (0), Withdraw (1)
- `flowRateUnit` uses integer codes (e.g. `100` = µL/min)

## Modbus helpers

Private: CRC-16, `buildReadRequest`, `buildWriteSingleRequest`,
`buildWriteMultipleRequest`, big-endian ABCD float ↔ two 16-bit registers,
`readHoldingRegisters`, `writeSingleRegister`,
`writeMultipleRegisters`.

## Threading

Each pump has its own `std::mutex` guarding serial I/O. UI calls are
synchronous; `pollStatus` is invoked from the Qt timer in
[[../frontend/SyringePumpTab]].

## Gotchas

- `getComPort(id)` is used by [[../frontend/Dialogs]] SyringePumpSettingsDialog
  to avoid double-assigning a COM port to both pumps.
- Dialog-provided `baudRate` and `modbusAddress` must match the hardware.
- See `docs/dLSP_pump.pdf` for pump protocol reference (shipped in repo).
