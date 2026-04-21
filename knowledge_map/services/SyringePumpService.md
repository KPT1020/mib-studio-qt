# SyringePumpService

> Modbus RTU syringe-pump control over serial, now supporting dynamic pump
> counts (1..N) via stable `PumpHandle` IDs.

**Source:** `src/backend/services/SyringePumpService.cpp`,
`include/backend/services/SyringePumpService.h`
**Related:** [[../frontend/SyringePumpTab]],
`src/standalone/pump_control/`

## Responsibility

- Own a dynamic list of pumps (`std::vector<shared_ptr<PumpConnection>>`)
  keyed by `PumpHandle`.
- Manage one `QSerialPort` per pump (connect/disconnect, poll status,
  control commands).
- Expose per-pump config and runtime status (`PumpConfig`, `PumpStatus`).
- Provide cross-platform serial APIs:
  - primary: `connect(handle, QString portName, ...)`
  - Windows compatibility: `connect(handle, int comPort, ...)` and
    `getComPort(handle)` wrappers.

## Public API shape

- Pump lifecycle:
  - `addPump(name)`, `removePump(handle)`, `clearPumps()`
  - `pumpHandles()`, `pumpCount()`, `hasPump(handle)`
- Per-pump control:
  - `setFlowRate`, `setDirection`, `start`, `stop`
  - `purge`, `stopPurge`, `setSyringeVolume`
- Per-pump status:
  - `pollStatus(handle)` (UI timer driven)
  - `getStatus(handle)`, `getConfig(handle)`, `setConfig(handle)`
  - `getPumpName` / `setPumpName`, `getPortName`

## Enums / units

- `RunStatus`: Stop (0), Forward (1), Backward (2), Pause (3)
- `Direction`: Infuse (0), Withdraw (1)
- Flow/syringe units continue to use integer pump register codes
  (e.g., `100` for uL/min and uL).

## Threading

- `pumpsMutex_` protects the container of pump connections.
- Each `PumpConnection` has its own `mutex` guarding serial I/O and status.
- UI operations are synchronous, and status polling still occurs via
  `SyringePumpTab` timer callbacks.

## Modbus helpers

Private helpers retained:
CRC16, frame builders, float/register conversion, and register read/write
helpers. Request/response logging now uses pump name + handle instead of
hardcoded Sample/Sheath labels.

## Gotchas

- Cross-platform port persistence is by serial **name** (`COM3`, `ttyUSB0`,
  `cu.usbserial-*`), not Windows COM number.
- Windows COM number methods are compatibility wrappers only.
- `addPump()` does not auto-connect; UI/config controls when ports open.
- `setFlowRate` and `setSyringeVolume` clamp to hardware register bounds.
- See `docs/dLSP_pump.pdf` for protocol reference.
