# SyringePumpTab

> UI for [[../services/SyringePumpService]]: multi-pump connect, flow rate,
> direction, start/stop/purge, live status. Sole settings surface — no
> separate dialog.

**Source:** `src/frontend/tabs/SyringePumpTab.cpp`,
`include/frontend/tabs/SyringePumpTab.h`
**Related:** [[../services/SyringePumpService]]

## Responsibility

- Hosts a dynamic list of `PumpRowWidget`s backed by
  `SyringePumpService::PumpHandle` (add/remove pump rows at runtime).
- Each row exposes the full `PumpConfig`: serial port (with refresh button),
  baud rate, Modbus address, syringe volume + unit, flow rate + unit,
  direction, plus connect/disconnect/start/stop/hold-to-purge.
- Connect-time fields (port, baud, address) lock while the pump is
  connected. Runtime fields (flow rate, direction, syringe volume)
  auto-apply with a short debounce and push to hardware.
- Poll timer calls `pollStatus(handle)` and refreshes: run status, current
  flow rate, accumulated volume, stall indicator.
- Persists per-pump config to `config.json` `pump_ports` array and migrates
  legacy `pump_sample_*` / `pump_sheath_*` keys.
- Port choices are populated per row via `backend::Tools::availableSerialPortNames()`;
  ports used by other pumps or externally reserved (e.g. autofocus) are
  filtered out. `PumpRowWidget::portRefreshRequested` triggers repopulation.

## Gotchas

- Main app still defaults to two pumps (`Sample`, `Sheath`); standalone
  `pump_control` defaults to one (`Pump 1`) when no `pump_ports` config exists.
- Port collision checks use normalized port names and a callback for
  externally reserved ports (autofocus reservations in the main app).
  When a pump's port changes, sibling rows' port lists are repopulated.
- `flowRateUnit` and `syringeVolumeUnit` are integer codes, not strings —
  `100` = µL / µL/min, `103` = mL / mL/min.
- Row status refresh runs every 500 ms. `PumpRowWidget::setViewState()` skips
  Modbus address writes while the address spin box has focus, preventing
  in-progress edits from being overwritten before `editingFinished` commits
  and persists the new value.
- See `docs/dLSP_pump.pdf` for the protocol.
