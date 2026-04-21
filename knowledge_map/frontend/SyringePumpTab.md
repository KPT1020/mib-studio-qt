# SyringePumpTab

> UI for [[../services/SyringePumpService]]: multi-pump connect, flow rate,
> direction, start/stop/purge, live status.

**Source:** `src/frontend/tabs/SyringePumpTab.cpp`,
`include/frontend/tabs/SyringePumpTab.h`
**Related:** [[../services/SyringePumpService]],
[[Dialogs]] (`SyringePumpSettingsDialog`)

## Responsibility

- Hosts a dynamic list of `PumpRowWidget`s backed by
  `SyringePumpService::PumpHandle` (add/remove pump rows at runtime).
- Each row supports connect/disconnect, flow-rate + unit, direction,
  start/stop, and hold-to-purge; values auto-apply with a short debounce.
- Poll timer calls `pollStatus(handle)` and refreshes: run status, current
  flow rate, accumulated volume, stall indicator.
- Persists per-pump config to `config.json` `pump_ports` array and migrates
  legacy `pump_sample_*` / `pump_sheath_*` keys.
- Delegates serial-port + syringe-volume configuration to
  `SyringePumpSettingsDialog`.

## Gotchas

- Main app still defaults to two pumps (`Sample`, `Sheath`); standalone
  `pump_control` defaults to one (`Pump 1`) when no `pump_ports` config exists.
- Port collision checks use normalized port names and a callback for
  externally reserved ports (autofocus reservations in the main app).
- `flowRateUnit` is an integer code, not a string — `100` = µL/min.
- See `docs/dLSP_pump.pdf` for the protocol.
