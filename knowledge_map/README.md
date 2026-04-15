# Knowledge Vault — mib-studio-qt

Obsidian-style agent knowledge base. Each note is short and atomic; navigate by
following `[[WikiLinks]]`. If you are a new agent, start here:
[[Agent-Onboarding]].

## Map of Content

### Architecture
- [[architecture/_MOC|Architecture MOC]]
- [[architecture/Overview]] — layered design (frontend → backend → services)
- [[architecture/AppBackend]] — composition root
- [[architecture/Threading-Model]]
- [[architecture/Data-Flow]]

### Services (`src/backend/services/`)
- [[services/_MOC|Services MOC]]
- Realtime path: [[services/CaptureService]] → [[services/ProcessingService]]
- Persistence: [[services/Hdf5Service]], [[services/SqliteService]]
- Playback: [[services/PlaybackService]]
- Hardware I/O: [[services/CameraControlService]], [[services/AutofocusService]],
  [[services/TriggerService]], [[services/SyringePumpService]]
- Optional: [[services/YoloService]], [[services/RecorderService]]

### Frontend (`src/frontend/`)
- [[frontend/_MOC|Frontend MOC]]
- [[frontend/MainWindow]], [[frontend/Controllers]]
- Tabs: [[frontend/ConnectTab]], [[frontend/PreviewPage]],
  [[frontend/ConfigTabs]], [[frontend/ExperimentMonitoringTab]],
  [[frontend/HdfReviewTab]], [[frontend/NanopositionerTab]],
  [[frontend/SyringePumpTab]]
- Support: [[frontend/Dialogs]], [[frontend/System-Utilities]]

### Camera (`src/camera/`)
- [[camera/_MOC|Camera MOC]]
- [[camera/ICamera]], [[camera/EGrabberCamera]], [[camera/MockCamera]]

### Data model
- [[data-model/FrameStore]]
- [[data-model/HDF5-Storage]]

### Domain
- [[domain/Glossary]]
- [[domain/Microscopy-Pipeline]]

### Build & Run
- [[build-and-run/Build]]
- [[build-and-run/Run-Modes]]
- [[build-and-run/Dependencies]]

### Conventions
- [[conventions/Code-Conventions]]
- [[conventions/Logging]]

### Current state
- [[current-state/Recent-Work]]
- [[current-state/Task-Log-Index]]

## Related

- `docs/` — user-facing how-tos (`docs/howto/*.md`) and integration notes
- `knowledge_map/task/` — 19 dated task records (historical design/debug notes)
- `CLAUDE.md` — top-level project instructions (read first)
