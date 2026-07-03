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
- `AppConfigWatcher` maps `image_processing` JSON ⇄ `ProcessingConfig`
  (read block applies to the live service; write block persists). New keys:
  `adaptive_threshold` + `otsu_scale` (per-frame Otsu segmentation),
  `focus_laplacian_min` and `filters.enable_focus_check` (topology-free focus
  gate). All default to the previous behaviour. Settings-dialog widgets for
  them are still a follow-up — set via config.json / profiles for now.
- See task `knowledge_map/task/2025-11-25-config-profiles.md` for the
  profile management history and current implementation notes.
- Bundled defaults now include top-level `config_schema_version`. Remote
  profile support should treat missing schema as legacy local config rather
  than rejecting existing user profiles at startup.
