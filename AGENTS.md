# Agent Guide — MIB Studio Qt

C++17 / Qt 6.7.3 desktop app for real-time microscopy image capture,
processing, and HDF5 experiment storage. OpenCV for image processing, Euresys
EGrabber for hardware cameras, optional ONNX Runtime YOLO segmentation.

This file is a map, not a manual. Deep knowledge lives in the vault and docs.

## Onboarding

1. [`knowledge_map/Agent-Onboarding.md`](knowledge_map/Agent-Onboarding.md) — guided ~10 min ramp-up
2. [`knowledge_map/README.md`](knowledge_map/README.md) — vault map of content
3. [`docs/golden-principles.md`](docs/golden-principles.md) — mechanical rules for this repo

## Key Navigation

| Task | Start here |
|------|------------|
| Architecture, threading, data flow | [`knowledge_map/architecture/Overview.md`](knowledge_map/architecture/Overview.md) |
| A backend service | `knowledge_map/services/<Name>Service.md` |
| A frontend tab/dialog/controller | `knowledge_map/frontend/` |
| Build, run modes, dependencies | [`knowledge_map/build-and-run/Build.md`](knowledge_map/build-and-run/Build.md) |
| Code and logging conventions | [`knowledge_map/conventions/Code-Conventions.md`](knowledge_map/conventions/Code-Conventions.md) |
| How-to guides and runbooks | [`docs/README.md`](docs/README.md) |
| Architecture decisions (ADRs) | [`docs/decisions/README.md`](docs/decisions/README.md) |
| Multi-PR or design-heavy work | [`docs/exec-plans/README.md`](docs/exec-plans/README.md) |
| Known debt | [`docs/exec-plans/tech-debt-tracker.md`](docs/exec-plans/tech-debt-tracker.md) |
| What shipped recently | [`knowledge_map/current-state/Recent-Work.md`](knowledge_map/current-state/Recent-Work.md) |

## Vault Maintenance (required)

Every code change must land with matching vault updates in the same
commit/PR. The source-file to vault-note mapping is in
[`knowledge_map/Vault-Maintenance.md`](knowledge_map/Vault-Maintenance.md).

## Build and Run

```bash
cmake --preset windows-default            # Windows, VS2022 x64, Conan toolchain
cmake --build build --config Debug
cmake --preset linux-backend-only         # Linux, backend lib + tests only
cmake --build --preset linux-backend-only-build
```

- `mib_studio_qt` — production app (hardware camera)
- `mock_studio_qt` — dev app with GUI mock camera selector
  (`MIB_CAMERA_MODE=mock`, `MIB_MOCK_CAMERA_DIR=<path>`; see
  [`knowledge_map/build-and-run/Run-Modes.md`](knowledge_map/build-and-run/Run-Modes.md))

## Verification

```bash
python3 scripts/check_docs.py                  # docs + vault wikilink integrity
ctest --preset linux-backend-only-test         # backend unit tests
```

CI: [`backend-ci.yml`](.github/workflows/backend-ci.yml) builds and tests the
backend on Linux; [`docs-ci.yml`](.github/workflows/docs-ci.yml) runs the
knowledge checks; [`ci.yml`](.github/workflows/ci.yml) validates Windows
packaging scripts.

## Hard Conventions

- **spdlog** for logging; never `std::cout` in app code.
- Headers mirror source layout under `include/`.
- Review existing `Tools` ([`src/backend/app/Tools.cpp`](src/backend/app/Tools.cpp)) before writing new utilities.
- Prefer ready-made EGrabber SDK patterns over hand-rolled camera code.
- Runtime data (logs, sqlite, HDF5, mock frames) lives under `data/`.
- Test performance metrics go to MLflow at `mlflow.yofo.bio` via
  `MLFLOW_TRACKING_USERNAME` / `MLFLOW_TRACKING_PASSWORD` env vars — never
  hardcode credentials.
