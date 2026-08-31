# SyringePumpService

> Dual-pump control (Sample + Sheath) via Modbus RTU over serial.

**Source:** `src/backend/services/SyringePumpService.cpp`,
`include/backend/services/SyringePumpService.h`
**Related:** [[SerialBus]] (transport), [[../frontend/SyringePumpTab]],
[[../frontend/Dialogs]] (SyringePumpSettingsDialog)

## Responsibility

- Maintain two independent pump connections (`PumpId::Sample`,
  `PumpId::Sheath`). Serial I/O goes through the shared [[SerialBus]]
  session for each adapter (acquired from the `AppBackend`-owned
  `SerialBusManager`), so a pump can share an RS485 adapter with other Modbus
  devices instead of failing on a second open. The public API still addresses
  adapters by Windows COM number; the `COMn` name is synthesized at this
  service's boundary only.
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

Public scan helper probes `REG_RUN_COMMAND` (`0x0001`) with Modbus function
`0x03` over an address range (default 1..8) and returns responsive addresses.

## Threading

Each pump has its own `std::mutex` guarding pump state; per-adapter frame
serialization and response correlation live in the shared [[SerialBus]]
session (bus mutex always innermost). UI calls are synchronous; `pollStatus`
is invoked from the Qt timer in [[../frontend/SyringePumpTab]].

## Gotchas

- `getComPort(id)` is used by [[../frontend/Dialogs]] SyringePumpSettingsDialog
  to avoid double-assigning a COM port to both pumps.
- Dialog-provided `baudRate` and `modbusAddress` must match the hardware.
- See `docs/dLSP_pump.pdf` for pump protocol reference (shipped in repo).
