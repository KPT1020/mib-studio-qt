# PumpControlMainWindow

> Standalone host window for the `pump_control` executable.

**Source:** `src/standalone/pump_control/PumpControlMainWindow.cpp`,
`include/standalone/pump_control/PumpControlMainWindow.h`
**Related:** [[SyringePumpTab]], [[Dialogs]], [[../services/SyringePumpService]]

## Responsibility

- Own an in-process `SyringePumpService` instance for standalone pump-only use.
- Ensure at least one pump exists on startup (`Pump 1`) when no config has been loaded yet.
- Host `SyringePumpTab` as the central widget.
- Provide a minimal menu:
  - `File -> Exit`
  - `Settings -> Pump Settings...` (`SyringePumpSettingsDialog`)

## Notes

- Unlike `mib_studio_qt`, standalone mode passes an empty reserved-port provider
  to pump UI/dialogs (no autofocus reservation coupling).
