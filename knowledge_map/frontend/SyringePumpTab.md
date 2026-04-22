# SyringePumpTab

> UI for [[../services/SyringePumpService]]: dynamic list of syringe
> pumps, each with its own connect / flow-rate / direction /
> start-stop-purge controls and live status.

**Source:** `src/frontend/tabs/SyringePumpTab.cpp`,
`include/frontend/tabs/SyringePumpTab.h`,
`src/frontend/widgets/PumpRowWidget.cpp`,
`include/frontend/widgets/PumpRowWidget.h`
**Related:** [[../services/SyringePumpService]],
[[Dialogs]] (`SyringePumpSettingsDialog`),
[[../build-and-run/Run-Modes]] (`pump_control` standalone app)

## Responsibility

- Host a scrollable list of `PumpRowWidget`s — one per pump registered
  with `SyringePumpService`.
- Top bar: **+ Add Pump** button and live pump count.
- 500 ms poll timer calls `PumpRowWidget::refresh()` on every row, which
  invokes `SyringePumpService::pollStatus(handle)` and repaints status,
  current flow rate, accumulated volume, and stall indicator.
- Persist pump list + per-pump live settings (flow rate, unit, direction)
  to `config.json` under the new `pumps: [...]` schema. Legacy flat keys
  (`pump_sample_*`, `pump_sheath_*`) are auto-migrated on first load.
- Constructor signature: `SyringePumpTab(SyringePumpService& svc,
  QWidget* parent)`. No dependency on `AppBackend`, so the tab works
  inside both `mib_studio_qt` (via `backend.syringePump()`) and the
  standalone `pump_control` exe.
- `ensureDefaultPumps(QStringList)` adds default pumps (e.g. `"Sample"`
  + `"Sheath"` in the main app, `"Pump 1"` in the standalone) when the
  service is empty on first run.

## PumpRowWidget

Self-contained per-pump row rendered as a `QGroupBox`:
- Editable pump name (also sets the group-box title).
- Connect / Disconnect, Start / Stop, Purge (hold).
- Flow rate + unit (µL/min, mL/min) + direction (Infuse / Withdraw)
  with 300 ms debounced auto-apply.
- Status, current flow, accumulated volume labels.
- "Remove" button that emits `removeRequested(handle)` back to the tab.

## Gotchas

- Each pump must be on a **different** serial port; the settings dialog
  enforces this by combining
  `SyringePumpService::reservedPortNames(handle)` with any external
  reservation callback (e.g. autofocus's COM port in MIB Studio).
- `flowRateUnit` is an integer code, not a string — `100` = µL/min,
  `103` = mL/min.
- Serial port names differ per OS: `COM3`, `/dev/ttyUSB0`,
  `/dev/cu.usbserial-...`. Users on Linux / macOS pick a name rather
  than a numeric COM index.
- See `docs/dLSP_pump.pdf` for the protocol.
