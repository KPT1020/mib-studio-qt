# Project Structure

This project is organized around a backend-first architecture. Backend modules
own camera acquisition, processing, recording, playback, persistence, and service
coordination. Frontends adapt backend state and commands into UI-specific
presentation.

## Current Layout

```text
src/
  backend/
    CMakeLists.txt
    camera/
    diagnostics/
    playback/
    services/
  frontend/
    qt/
      CMakeLists.txt
    controllers/
    core/
    dialogs/
    models/
    system/
    tabs/
    utils/
    widgets/
  bridge/
    CMakeLists.txt

include/
  backend/
    camera/
    diagnostics/
    playback/
    services/
  bridge/
  frontend/

tests/
  CMakeLists.txt
  backend/
  camera/
  integration/
  processing/
  recording/
  python/

cmake/
  options.cmake
  dependencies.cmake
  packaging.cmake
```

The root `CMakeLists.txt` owns project metadata and subdirectory orchestration.
Module targets are declared next to the subsystem that owns them:

- `src/backend/CMakeLists.txt` defines `mib_backend`.
- `src/frontend/qt/CMakeLists.txt` defines `mib_studio_qt`.
- `src/bridge/CMakeLists.txt` defines frontend-neutral bridge API targets.
- `tests/CMakeLists.txt` defines CTest coverage by subsystem.

## Dependency Direction

```text
tests
  |
  v
backend/core <----------------+
  ^                           |
  |                           |
  +-- frontend/qt adapters    |
  |                           |
  +-- bridge/cpp_api/tauri ---+
```

Detailed data/control flow:

```text
camera SDKs / mock frames / filesystem / database
          |
          v
backend camera, processing, recording, playback services
          |
          v
backend application facade (backend::AppBackend)
          |
          v
frontend adapters
     +----+----+
     |         |
  Qt widgets  Tauri bridge
```

## Ownership Rules

- Put reusable application logic under `src/backend` and `include/backend`.
- Put Qt widget/dialog/tab/controller code under `src/frontend` and
  `include/frontend`.
- Put frontend-neutral command/event types under `include/bridge` and bridge
  build targets under `src/bridge`.
- Put C++ tests under `tests/<subsystem>` and register them in
  `tests/CMakeLists.txt`.
- Keep generated/local preset files out of version control; project-wide presets
  live in `CMakePresets.json`.

## Mock Hardware Workflow

Mock camera support is a first-class backend workflow. Linux backend-only builds
must not require EGrabber/Coremor hardware SDKs.

```text
cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build
ctest --preset linux-backend-only-test --output-on-failure
```
