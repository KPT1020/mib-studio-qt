# PumpControlMainWindow

> Standalone host window for the `pump_control` executable.

**Source:** `src/standalone/pump_control/PumpControlMainWindow.cpp`,
`include/standalone/pump_control/PumpControlMainWindow.h`
**Related:** [[SyringePumpTab]], [[../services/SyringePumpService]]

## Responsibility

- Own an in-process `SyringePumpService` instance for standalone pump-only use.
- Ensure at least one pump exists on startup (`Pump 1`) when no config has been loaded yet.
- Host `SyringePumpTab` as the central widget. All per-pump settings
  (port, baud, address, syringe volume, flow rate, direction) live in the
  tab's rows — there is no separate settings dialog.
- Provide a minimal menu:
  - `File -> Exit`

## Notes

- Unlike `mib_studio_qt`, standalone mode passes an empty reserved-port provider
  to the pump tab (no autofocus reservation coupling).
