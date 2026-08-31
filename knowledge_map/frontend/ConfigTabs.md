# ConfigTabs

> Docked panel with multiple sub-tabs for tuning: processing config, JSON
> table viewer, camera JS script loader, ROI and monitoring settings. Also
> owns the profile selector/actions for local and R2-backed profile catalogs.

**Source:** `src/frontend/tabs/ConfigTabs.cpp`,
`include/frontend/tabs/ConfigTabs.h`
**Related:** [[../services/ProcessingService]]::`ProcessingConfig`,
[[../frontend/System-Utilities]] (`AppConfigWatcher`, `ProfileManager`),
[[Dialogs]] (`ProcessingSettingsDialog`, `MonitoringSettingsDialog`)

## Responsibility

- Let the user edit `ProcessingConfig` fields (thresholds, gates,
  multi-image, target-group, ring-ratio).
- Drive camera-side GenICam scripts via
  [[../services/CameraControlService]]`::applyScriptToDevice` (through
  `AppBackend::applyCameraScriptFromFile`).
- Parse EGrabber scripts using `utils/EgrabberConfigParser.cpp`.
- MindVision tab ("MindVision config (mindvisionConfig.json)"): JSON editor
  with Reset/Save/Browse/Clear (QSettings key
  `Config/ExternalMindVisionConfigPath`, default seeded from
  `:/defaults/mindvisionConfig.json`), **Apply to Camera** →
  `AppBackend::applyMindVisionConfigFromFile` (guarded on
  `isMindVisionCameraSelected()`; stops capture, rebuilds factory), **Soft
  Trigger** → `AppBackend::softTriggerCamera` (acquisition trigger — distinct
  from the sort-pulse buttons in [[ExperimentMonitoringTab]]), and a pulse
  generator group driving [[../services/PulseGeneratorService]]. The group
  separates **Port / bus settings / Slave address / Channel**: a
  `QSerialPortInfo`-populated port dropdown (system name + description +
  USB S/N + VID:PID) with an explicit Refresh, baud/data/parity/stop combos,
  Modbus address spin, a read-only **Scan** (addresses 1–16, worker thread,
  cancelable, classifies generators vs generic Modbus devices vs
  corrupt/collision responses; never writes), Connect (typed `LinkError`
  status on failure), channel, frequency 400–40000 Hz defaulting to 5000 Hz
  = the 5000 fps bench trigger rate, duty %, Set/Start/Stop. Settings persist
  in the QSettings group `PulseGenerator` (port name **plus USB
  serial/VID/PID** so a renamed `/dev/ttyUSB*` node re-resolves when the
  identity matches exactly one port; ambiguous matches force operator
  selection). `~ConfigTabs` cancels/joins any running scan thread.
- MindVision "Trigger & strobe parameters" form: combos/spinboxes for
  trigger mode, edge type, exposure (0.8–838860 µs = MV-XGC51 sensor range),
  trigger delay/jitter/count, strobe mode/delay/width/polarity. **Two-way
  synced with the JSON editor** (`syncMvFormFromJson` /
  `syncMvJsonFromForm`, 150 ms debounce on editor edits, `mvSyncGuard_`
  breaks recursion): widget edits rewrite only their keys into the current
  JSON (untouched keys like ROI/gain survive; QJson alphabetizes on
  rewrite); Save/Apply always read the editor text, so the form never
  bypasses the config file. Mid-edit invalid JSON leaves the form at its
  last good state.
- Render/edit JSON config using `models/JsonTableModel.cpp` and
  `utils/JsonFlatten.cpp`.
- Persist config via `utils/ConfigPathManager.cpp` and
  [[../frontend/System-Utilities]] `AppConfigWatcher`.
- Manage profiles, lazy `profile.meta.json` generation, manual catalog
  checks, checksum-verified updates, and field-level diffing through
  [[../frontend/System-Utilities]] `ProfileManager`.
- Profile catalogs for manual remote updates are published outside the app via
  `publish-profiles.py` to
  `https://updates.yofo.bio/profiles/<channel>/catalog.json`; see
  `docs/howto/auto-update-r2.md`.

## Gotchas

- Parameter-tuning panel must stay in sync bidirectionally with the config
  table (see recent fix in
  `git log`: "fix: sync param tuning panel with config table").
- External config changes while the JSON editor has unsaved edits now set a
  visible stale/conflict warning instead of silently leaving the editor and
  table behind the backend state.
- JSON table rebuild is shared with `frontend::jsonutil` so the round-trip
  semantics for nested objects, arrays of objects, and arrays of scalars
  stay testable in one place.
- Profile state is now schema-aware: local profiles get a generated
  `profile.meta.json`, remote-managed profiles show update/incompatibility
  state, and the manual update flow stages downloads before replacing local
  files.
- Some gates require their `enable_*` flag too (e.g.
  `enable_ring_ratio_check`). Editing thresholds alone won't change
  classification.
- See task `knowledge_map/task/2025-11-25-config-profiles.md` for the
  profile management history and current implementation notes.
- Bundled defaults now include top-level `config_schema_version`. Remote
  profile support should treat missing schema as legacy local config rather
  than rejecting existing user profiles at startup.
