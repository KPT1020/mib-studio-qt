# 2026-06-01 — Backend-only build/test mode

## Context

Need a repeatable environment/setup path to compile backend code and run
backend tests without building frontend executables.

## Changes

- Added new CMake option:
  - `MIB_BUILD_BACKEND_ONLY` (default `OFF`)
  - When `ON`, frontend executable target generation is skipped.
- Added backend smoke test target:
  - `mib_backend_smoke_test` (`src/tests/backend_smoke_test.cpp`)
  - Registered with CTest as `backend.smoke` and label `backend`.
- Added Linux presets:
  - configure: `linux-backend-only`
  - build: `linux-backend-only-build`
  - test: `linux-backend-only-test`
- Updated docs:
  - `docs/howto/linux-build.md`
- Updated build/run vault docs:
  - `build-and-run/Build.md`
  - `build-and-run/Dependencies.md`
  - `build-and-run/Run-Modes.md`

## Usage

```bash
cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build --target mib_backend mib_backend_smoke_test
ctest --preset linux-backend-only-test -L backend --output-on-failure
```

## Notes

- Backend-only mode still links Qt Core/Gui/SerialPort because backend services
  use Qt types.
- Frontend-only Qt modules (Widgets/Charts/Network/Concurrent) are not required
  in backend-only configure/build.
