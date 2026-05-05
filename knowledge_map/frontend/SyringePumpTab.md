# SyringePumpTab

> UI for [[../services/SyringePumpService]]: dual-pump connect, flow rate,
> direction, start/stop/purge, live status.

**Source:** `src/frontend/tabs/SyringePumpTab.cpp`,
`include/frontend/tabs/SyringePumpTab.h`
**Related:** [[../services/SyringePumpService]],
[[Dialogs]] (`SyringePumpSettingsDialog`)

## Responsibility

- Per-pump (`Sample`, `Sheath`) controls: connect button, flow-rate
  spinbox + unit combo, direction, start/stop, purge.
- Poll timer calls `pollStatus(PumpId)` and refreshes: run status, current
  flow rate, accumulated volume, stall indicator.
- Delegate COM port / baud / Modbus address configuration to
  `SyringePumpSettingsDialog`.
- `SyringePumpSettingsDialog` now includes per-pump baud/address fields and
  in-dialog address scan; tab connect buttons consume those persisted values.
- On pump connect, the tab applies persisted syringe metadata from config:
  volume + unit and inner diameter (converted to cross-sectional area).

## Gotchas

- Sample and Sheath pumps must be on **different** COM ports; the settings
  dialog uses `SyringePumpService::getComPort(PumpId)` to enforce.
- `flowRateUnit` is an integer code, not a string — `100` = µL/min.
- Syringe inner diameter is configured in `SyringePumpSettingsDialog` and
  persisted as `pump_{sample|sheath}_inner_diameter_mm`.
- See `docs/dLSP_pump.pdf` for the protocol.
