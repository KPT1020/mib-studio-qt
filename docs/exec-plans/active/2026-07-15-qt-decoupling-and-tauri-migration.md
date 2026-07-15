# Qt decoupling and React + Tauri migration

Status: active (2026-07-15) — Phase 1 slice 1 landed; backend still links Qt.

Tracks epic #246. The platform decision is recorded in ADR
[`../../decisions/0001-react-tauri-migration.md`](../../decisions/0001-react-tauri-migration.md).
This plan is the living breakdown that would otherwise be GitHub sub-issues:
the Qt inventory, the feature-parity matrix, the performance budgets, and the
incremental PR order.

## Goal

Ship one supported MIB Studio desktop app on React + Tauri v2 with a **Qt-free
C++ backend** and a Qt-free backend build/test lane, preserving every operator
workflow in [`../../manual/`](../../manual/), data compatibility, reliability,
and performance. Migrate in vertical slices from current `main`; keep the Qt
shell building until Tauri passes the epic's exit gate.

## Principles (from ADR 0001)

1. Start from `main`; do not merge PR #59 wholesale.
2. Every slice is usable, tested, and benchmarked end to end.
3. Backend contracts carry no Qt types; the UI-neutral seam is
   `backend::bridge::BackendFacade`.
4. Protect the frame hot path — a per-frame PNG/base64 transport is not the
   production design unless it meets the budgets below.
5. Preserve HDF5 files, profiles, config, update channels, logs, and crash
   reporting, or migrate through tested tooling.

## Qt dependency inventory (backend)

No `Q_OBJECT`/moc in backend headers; threading is already std. Qt survives in
these clusters. "Contract" = Qt appears in a public header.

| # | Cluster | Files | Qt used | Contract? | Replacement | Status |
|---|---------|-------|---------|-----------|-------------|--------|
| 1 | Modbus framing | `include/backend/services/ModbusRtu.h` | `QByteArray` | yes | `std::vector<uint8_t>` | **done (this PR)** |
| 2 | MindVision JSON | `include/backend/camera/mindvision/MindVisionConfig.h` (+ `MindVisionCamera.cpp`, `CameraControlService.cpp` reads) | Qt JSON, `QFile` | yes | `nlohmann_json` + `std::ifstream` | **done (this PR)** |
| 3 | Syringe-pump serial I/O | `src/backend/services/SyringePumpService.cpp` | `QSerialPort`, `QByteArray` (at seam) | no (header now Qt-free) | platform-neutral serial interface | pending |
| 4 | LUT catalog | `include/backend/processing/EModulusLutCatalog.{h,cpp}` | `QNetworkAccessManager`, `QStandardPaths`, `QDir/QFile/QSaveFile`, `QDateTime`, `QUrl`, `QCryptographicHash`, `QEventLoop/QTimer` | yes | UI-neutral HTTP/paths seam, or move fetch to Rust shell; `std::filesystem`; a hashing lib | pending (largest; needs the networking-seam decision) |
| 5 | Mock-camera decode | `src/backend/camera/mock/MockCamera.cpp` | `QImage`/`QImageReader` | no | OpenCV `imread` (fallback already present) | pending |
| 6 | Crash-reporter glue | `src/backend/app/CrashReporter.cpp` (+ incidental `QString` in `AppBackend.cpp`) | `qtMessageHandler`, `QString` | no | spdlog-native sink; std::string paths | pending (falls away with #4) |

When clusters 3–6 land, drop `Qt6::*` and `AUTOMOC` from
`src/backend/CMakeLists.txt` and stop `find_package`-ing Qt for
`MIB_BUILD_BACKEND_ONLY` in `cmake/MIBDependencies.cmake` — that is the Phase 1
exit gate (`linux-backend-only` builds with no Qt SDK).

## Feature-parity matrix

Behavioural parity is the target; deliberate UX changes are tracked separately.

| Workflow | Manual page | Qt | Tauri |
|----------|-------------|----|-------|
| Install + update | getting-started | ships | not started |
| Connect: EGrabber / MindVision / mock | connect | ships | not started |
| Live view, zoom/pan, ROI, overlays, status | acquire-and-record | ships | not started |
| Processing settings, profiles, trust gates, preview | acquire-and-record | ships | not started |
| Experiment lifecycle, monitoring charts, HDF5 recording | acquire-and-record | ships | not started |
| Review, playback, metrics, image/CSV export, reanalysis | review-and-postprocess | ships | not started |
| Autofocus, nanopositioner, syringe pump, trigger | (hardware dialogs) | ships | not started |
| Logs, crash reports, docs, problem reporting | troubleshooting | ships | not started |

## Baseline performance budgets

Fill the "Qt baseline" column from measurements on reference hardware; Tauri
must match or improve each before cutover.

| Metric | Qt baseline | Tauri target |
|--------|-------------|--------------|
| Capture frame rate (fps) | TBD | ≥ baseline |
| Displayed frame rate (fps) | TBD | ≥ baseline |
| Display latency (ms) | TBD | ≤ baseline |
| Dropped frames (%) under load | TBD | ≤ baseline |
| CPU (%) at reference capture | TBD | ≤ baseline |
| Memory (RSS) steady-state | TBD | ≤ baseline |
| Processing throughput | TBD | ≥ baseline |
| HDF5 write throughput | TBD | ≥ baseline |
| Startup / shutdown time (s) | TBD | ≤ baseline |
| Long-duration soak stability | TBD | ≥ baseline |

## Incremental PR strategy

Phase 1 — backend Qt-free (one PR per cluster, each with tests + vault):

1. **[this PR]** ModbusRtu.h + MindVisionConfig.h contracts → std/nlohmann.
2. Serial abstraction: `QSerialPort` behind a platform-neutral interface;
   port `SyringePumpService` and `scanModbusAddresses`.
3. LUT-catalog networking/paths seam (cluster 4) — requires the HTTP-seam vs
   move-to-Rust decision (own ADR).
4. Mock-camera decode → OpenCV (cluster 5).
5. Crash-reporter/`QString` glue (cluster 6).
6. Drop `Qt6::*` + `AUTOMOC` from the backend; make `MIB_BUILD_BACKEND_ONLY`
   configure/build/test with no Qt SDK. **Phase 1 exit gate.**

Phase 2 defines the production Rust ↔ C++ bridge (own ADR); Phase 3 is the
first Tauri vertical slice (mock camera end to end); Phase 4 migrates the
remaining workflows; Phase 5 packages, documents, and cuts over.

**PR #59** stays open as reference; it is superseded when the first production
Tauri slice (Phase 3) lands, at which point it is closed with a pointer here.
