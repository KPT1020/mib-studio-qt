# NanopositionerTab

> UI for [[../services/AutofocusService]]: COM port selection, manual
> voltage nudging, autofocus enable, status display.

**Source:** `src/frontend/tabs/NanopositionerTab.cpp`,
`include/frontend/tabs/NanopositionerTab.h`
**Related:** [[../services/AutofocusService]]

## Responsibility

- COM port enumeration + `probeComPort` for health checks.
- Connect/disconnect; display connection status.
- Manual voltage control (buttons drive `increaseVoltage`/`decreaseVoltage`).
- Autofocus toggle (`setEnabled`).
- Live display of running average / median ring ratio and last update
  timestamp.
- Config editor for the `AutofocusService::Config` struct (focus setpoint,
  range, step sizes, direction).

## Gotchas

- This tab moved into the Config area recently — see task
  `knowledge_map/task/2025-11-19-nanopositioner-tab.md`.
- Disconnecting applies `safeShutdownVoltage` before closing the port.
- The autofocus status callback fires on the backend control thread; it
  marshals to the GUI thread via `QMetaObject::invokeMethod(this, …,
  Qt::QueuedConnection)` and is cleared in the destructor — touching
  `ui->statusLabel` directly from the worker crashed intermittently.
