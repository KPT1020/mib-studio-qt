# Testing Strategy

This document describes how to validate changes while preserving the backend
and frontend boundaries documented in this architecture set.

## Test Layers

| Layer | Current entry point | Use when |
| --- | --- | --- |
| Documentation checks | Markdown/content review plus `git diff --check` | Docs-only changes, architecture updates, runbooks, and review evidence generation. |
| Backend configure/build | `cmake --preset linux-backend-only` and `cmake --build --preset linux-backend-only-build` | Backend services, camera abstractions, processing, storage, and CTest targets on Linux. |
| Backend CTest | `ctest --preset linux-backend-only-test` | Service behavior, processing pipelines, HDF5/playback behavior, and backend regressions. |
| Windows app build | `cmake --preset windows-default` and `cmake --build --preset windows-default-build` or release preset | Qt Widgets app, packaging-sensitive code, Windows hardware SDK paths, and installer/runtime changes. |
| Windows CTest | `ctest --preset windows-debug-test` or `ctest --preset windows-debug-integration-test` | Windows-specific backend validation and integration tests. |
| Script/tool checks | Script-specific commands under `scripts/` or `tools/` | Standalone post-processing tools, Python helpers, and generated evidence workflows. |
| Manual/runtime evidence | Screenshots, recordings, sample images, metrics, and logs in a review bundle | UI, CV/image-processing, pipeline, hardware, or workflow changes where command output alone is insufficient. |

## Current Registered C++ Tests

The root CMake file registers backend tests when `BUILD_TESTING` is enabled.
Current test names include:

- `backend.smoke`
- `backend.processing_batch_pipeline`
- `backend.kin10_hf_dataset_pipeline`
- `backend.kin6_mib_app_capture_proof`
- `backend.processing_multi_object`
- `backend.processing_object_tracking`

These tests link `mib_backend`. That is intentional: backend behavior should be
validatable without launching the Qt Widgets application.

## Validation By Change Type

### Docs-Only Changes

For architecture and runbook-only changes:

- confirm the documented paths and target names exist in the repository
- run `git diff --check`
- run a structural check that required markdown files are present and non-empty
- generate review evidence with `report.html`, `manifest.json`, command logs,
  and a flow diagram when the ticket asks for one
- confirm `git diff --name-only` contains only expected documentation or review
  artifact files

Docs-only changes do not require CMake configure/build unless they alter build,
source, test, or runtime files.

### Backend Changes

For backend code, camera code, processing, storage, diagnostics, or public
backend headers:

1. Configure the backend-only build:

   ```bash
   cmake --preset linux-backend-only
   ```

2. Build the touched backend target or the backend-only preset:

   ```bash
   cmake --build --preset linux-backend-only-build
   ```

3. Run targeted CTest first, then the broader backend suite:

   ```bash
   ctest --preset linux-backend-only-test -R <test-name> --output-on-failure
   ctest --preset linux-backend-only-test --output-on-failure
   ```

4. Capture configure, build, and test logs with exit codes in the review bundle.

### Frontend Changes

For Qt Widgets, UI resources, tabs, dialogs, frontend controllers, models, or
app startup:

- build the app target through the relevant Windows or Linux preset available
  in the environment
- run backend tests when the frontend path calls or changes backend behavior
- launch the app when possible and capture screenshots or a short demo for the
  changed path
- record launch failures as evidence instead of silently skipping runtime
  validation

### Pipeline, CV, Or Image-Processing Changes

For processing, segmentation, HDF5 pipeline, and visual output changes:

- run targeted processing tests
- use representative samples, not a single happy path
- save input, mask/output, overlay/contour images, and per-sample metrics
- include aggregate metrics and the exact regeneration command in
  `manifest.json`

### Hardware Or External Integration Changes

For EGrabber, Coremor, syringe pump, updater, Sentry, RustFS, or network-backed
integrations:

- keep hardware-independent logic covered by backend tests where possible
- document unavailable hardware or credentials in the review bundle
- run mock-mode or stub-mode validation on Linux when hardware is unavailable
- run Windows/hardware validation when the change cannot be proven through
  mocks or stubs

## Adding Tests With New Code

Use these placement rules:

- Add backend service tests under `src/tests/` and register them with CTest.
- Link backend tests to `mib_backend`, not `mib_studio_qt`.
- Keep integration tests labeled `integration` so fast presets can exclude
  network or dataset-backed work.
- Keep small committed fixtures under `tests/fixtures/` when needed.
- Keep generated test outputs under `build/test-output/` or
  `review_artifacts/`; both are local artifact locations and should not become
  runtime inputs.
- Add Python tests under `tests/` when validating Python scripts or fixtures.

## Review Evidence Expectations

Every review bundle should include enough evidence for a reviewer to audit the
change without reading the agent transcript:

- exact commands, timestamps, exit codes, working directory, and logs
- source commit and branch metadata
- `report.html` for human review
- `manifest.json` for machine-readable artifact provenance
- diagrams for architecture, data-flow, pipeline, UI-flow, or orchestration
  changes
- samples and metrics for image-processing or UI-visible work

For this documentation-only architecture phase, the required evidence is a docs
consistency review, `git diff --check`, a structural markdown check, an
architecture flow diagram, `report.html`, and `manifest.json`.
