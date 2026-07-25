# ChecklistPanel

> Generic renderer for [[../services/OperatorChecks]] check items: status
> glyph + status text (never color alone), label, detail, and either a
> `Fix...` recovery button or — in override-selection mode — an
> `Override` checkbox per overridable failed/warned item.

**Source:** `src/frontend/widgets/ChecklistPanel.cpp`,
`include/frontend/widgets/ChecklistPanel.h`
**Related:** [[MainWindow]] (preflight panel on the Connect tab),
[[../services/OperatorChecks]]

## API

- `setItems(vector<CheckItem>)` — re-render; skips the rebuild when the
  items are unchanged so periodic refreshes don't destroy buttons
  mid-interaction.
- `setOverrideSelectionEnabled(bool)` — readiness-dialog mode: overridable
  rows get an `Override` checkbox (state preserved across refreshes),
  non-overridable failures show "(cannot be overridden)".
- `checkedOverrideIds()` — the override selections.
- Signals: `recoveryRequested(checkId)`, `overrideSelectionChanged()`.

## Usage

- Preflight (UX-3): [[MainWindow]] wraps one in a "Hardware preflight"
  group box appended to the Connect tab and feeds it from
  `refreshWorkflowState()`; recovery ids route to auto-connect, the
  Processing Core dialog, the data folder, or the Profiles surface.
