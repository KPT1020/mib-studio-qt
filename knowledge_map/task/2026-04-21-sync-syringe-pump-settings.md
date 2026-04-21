# 2026-04-21 — Sync syringe pump settings with the modular tab

## Problem

`SyringePumpSettingsDialog` was hardcoded to exactly two pumps ("Sample" and
"Sheath") and wrote directly to `config.json`'s `pump_ports[0]` and
`pump_ports[1]`. It never went through `SyringePumpService::setConfig`, and
every Apply reset the `name` field back to the hardcoded strings. Once
`SyringePumpTab` became modular (dynamic N pumps via `addPump`/`removePump`),
the dialog was out of sync: custom pump names were clobbered on every Apply,
3rd+ pumps were invisible, and changes to port / baud / address never
propagated to the service's in-memory state.

## Decision

Delete the dialog entirely. Expose the full `PumpConfig` inside every
`PumpRowWidget` so the tab is the single settings surface.

## Changes

- `resources/ui/PumpRowWidget.ui` — added port combo + refresh button,
  baud combo (9600..115200), Modbus address spinbox (1-247), syringe volume
  spinbox, syringe unit combo.
- `include/frontend/widgets/PumpRowWidget.h`,
  `src/frontend/widgets/PumpRowWidget.cpp` — extended `ViewState` with
  `portName`, `baudRate`, `modbusAddress`, `syringeVolume`,
  `syringeVolumeUnit`. Added `setPortChoices()`, `portRefreshRequested` +
  `settingsChanged` signals. `setConnected()` locks port/baud/address while
  connected; syringe volume/unit remain editable.
- `src/frontend/tabs/SyringePumpTab.cpp` — new
  `populatePortChoices(row)` helper using
  `backend::Tools::availableSerialPortNames()` + `reservedPortNamesExcluding`.
  `settingsChanged` routes through `setConfig` + `saveConfig` and re-fires
  port population on sibling rows. If syringe volume changed and the pump
  is connected, also pushes via `setSyringeVolume`.
- Deleted: `src/frontend/dialogs/SyringePumpSettingsDialog.{cpp,h}`,
  `resources/ui/SyringePumpSettingsDialog.ui`.
- `CMakeLists.txt` — dropped the three dialog source entries.
- `resources/ui/MainWindow.ui` — dropped `syringePumpSettingsAct`.
- `src/frontend/core/MainWindow.cpp` — removed include + menu
  connect block.
- `src/standalone/pump_control/PumpControlMainWindow.cpp` — removed
  "Settings → Pump Settings..." action; tab is now the only config surface.

## Vault

- Removed `SyringePumpSettingsDialog` entry from
  `knowledge_map/frontend/Dialogs.md`.
- Rewrote `knowledge_map/frontend/SyringePumpTab.md` to document the full
  per-row config and the `populatePortChoices` flow.
- Updated `knowledge_map/frontend/PumpControlMainWindow.md` and
  `knowledge_map/services/SyringePumpService.md` to drop dialog
  references.
- Updated `knowledge_map/build-and-run/Run-Modes.md`.
- Appended an entry to `knowledge_map/current-state/Recent-Work.md`.
