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
  `purge`, `stopPurge`, `setSyringeVolume`,
  `setSyringeInnerArea`, `setSyringeInnerDiameterMm`.
- Per-pump status polling: `pollStatus(id)` — UI timer drives this.
- Expose config/status structs (`PumpConfig`, `PumpStatus`).
- Provide `scanModbusAddresses(comPort, baudRate, start, end, timeoutMs)` for
  settings-time address discovery on a selected serial port.

## Enums

- `RunStatus`: Stop (0), Forward (1), Backward (2), Pause (3)
- `Direction`: Infuse (0), Withdraw (1)
- `flowRateUnit` uses integer codes (e.g. `100` = µL/min)
- Syringe cross-sectional area uses Modbus registers `0x0063` (value)
  and `0x0064` (unit code). `setSyringeInnerDiameterMm` converts from
  millimeter diameter to area and picks a valid area unit scale so the
  encoded value stays within `1..9999`.

## Modbus helpers

Private: CRC-16, `buildReadRequest`, `buildWriteSingleRequest`,
`buildWriteMultipleRequest`, big-endian ABCD float ↔ two 16-bit registers,
`readHoldingRegisters`, `writeSingleRegister`,
`writeMultipleRegisters`.

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
- If you need repeatable flow behavior across syringe models, ensure the
  per-pump inner diameter is set (it drives area registers `0x0063/0x0064`).
- See `docs/dLSP_pump.pdf` for pump protocol reference (shipped in repo).
