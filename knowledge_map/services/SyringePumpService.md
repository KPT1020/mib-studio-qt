# SyringePumpService

> Dynamic N-pump control via Modbus RTU over serial. Cross-platform
> (Windows / Linux / macOS) via Qt's `QSerialPort`.

**Source:** `src/backend/services/SyringePumpService.cpp`,
`include/backend/services/SyringePumpService.h`
**Related:** [[../frontend/SyringePumpTab]],
[[../frontend/Dialogs]] (SyringePumpSettingsDialog),
[[../build-and-run/Run-Modes]] (`pump_control` standalone app)

## Responsibility

- Maintain any number of independent `QSerialPort` connections, one per
  registered pump. Pumps are added / removed at runtime via
  `addPump(name)` / `removePump(id)`.
- Per-pump control: `setFlowRate`, `setDirection`, `start`, `stop`,
  `purge`, `stopPurge`, `setSyringeVolume`.
- Per-pump status polling: `pollStatus(id)` — UI timer drives this from
  the pump row widget.
- Expose config/status structs (`PumpConfig`, `PumpStatus`).

## Key public API

```cpp
using PumpHandle = int;

PumpHandle addPump(const std::string& name);
bool       removePump(PumpHandle id);
int        pumpCount() const;
std::vector<PumpHandle> pumpHandles() const;
std::string pumpName(PumpHandle id) const;

// Cross-platform connect — portName is "COM3" on Windows,
// "/dev/ttyUSB0" on Linux, "/dev/cu.usbserial-..." on macOS.
bool connect(PumpHandle, const QString& portName, int baud, uint8_t addr);
// Windows convenience wrapper ("COM<n>" string).
bool connect(PumpHandle, int comPort,      int baud, uint8_t addr);

QString     getPortName(PumpHandle id) const;
QStringList reservedPortNames(PumpHandle exclude = -1) const;
```

## Enums

- `RunStatus`: Stop (0), Forward (1), Backward (2), Pause (3)
- `Direction`: Infuse (0), Withdraw (1)
- `flowRateUnit` uses integer codes (`100` = µL/min, `103` = mL/min)

## Modbus helpers

Private: CRC-16, `buildReadRequest`, `buildWriteSingleRequest`,
`buildWriteMultipleRequest`, big-endian ABCD float ↔ two 16-bit
registers, `readHoldingRegisters`, `writeSingleRegister`,
`writeMultipleRegisters`.

## Threading

Pumps are stored in a `std::map<PumpHandle, std::shared_ptr<Pump>>`
guarded by `pumpsMutex_`. Each `Pump` also has its own
`std::mutex` guarding serial I/O, so pumps on different ports can be
driven in parallel. Lookups copy the `shared_ptr` under the map mutex
to keep the `Pump` alive even if another thread removes it
concurrently. UI calls are synchronous; `pollStatus` is invoked from
the Qt timer in [[../frontend/SyringePumpTab]].

## Consumers

- [[../frontend/SyringePumpTab]] — dynamic list of pumps, Add/Remove.
- [[../frontend/Dialogs]] — `SyringePumpSettingsDialog` manages per-pump
  connection parameters and assigns unique COM ports by default.
- Standalone `pump_control` app — owns a `SyringePumpService` directly
  (no `AppBackend`).

## Gotchas

- `reservedPortNames()` returns the port names of all currently-connected
  pumps (optionally excluding one handle). The settings dialog combines
  this with any external reservation (e.g. autofocus's COM port in the
  main MIB Studio app) to keep COM port assignments unique by default.
- `QSerialPortInfo::availablePorts()` requires udev rules / `dialout`
  group membership on Linux — users without perms will see an empty
  combo.
- `setSyringeVolume` is remembered in the config even when disconnected;
  the cached value is pushed to the pump on next `connect()`.
- See `docs/dLSP_pump.pdf` for pump protocol reference (shipped in repo).
