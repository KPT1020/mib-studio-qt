# Linux Build

Use this path for fast local compile verification when changing portable app
code. It deliberately disables Windows-only hardware SDKs and installer
packaging, so it does not require EGrabber, Coremor, MSVC, windeployqt, or
InnoSetup.

## Prerequisites

- CMake 3.21+
- A C++17 compiler toolchain
- Ninja

For the fastest local loop on Ubuntu, install system development packages:

```bash
sudo apt install cmake build-essential ninja-build pkg-config git \
  qt6-base-dev qt6-charts-dev qt6-serialport-dev \
  libopencv-dev libhdf5-dev libspdlog-dev nlohmann-json3-dev libsqlite3-dev
```

For backend-only build/test loops (no frontend binaries), this smaller set is
enough:

```bash
sudo apt install cmake build-essential pkg-config git \
  qt6-base-dev qt6-serialport-dev \
  libopencv-dev libhdf5-dev libspdlog-dev nlohmann-json3-dev libsqlite3-dev
```

## Configure And Build

Fast local build from system packages:

```bash
cmake --preset linux-system-release
cmake --build --preset linux-system-release-build
```

Conan-based build:

```bash
conan install . --profile:host=conan/profiles/linux-gcc13 --profile:build=default --output-folder=build/linux --build=missing
cmake --preset linux-release
cmake --build --preset linux-release-build
```

The `linux-release` preset sets:

- `MIB_ENABLE_HARDWARE_SDKS=OFF`
- `MIB_ENABLE_WINDOWS_PACKAGING=OFF`
- `MIB_USE_SENTRY=OFF`

That builds the application with the existing mock/stub paths for proprietary
hardware and local-only crash reporting.

## Processing-only build (no Qt / cloud agent)

Use this when the environment has **no Qt6** — e.g. a freshly cloned cloud
container, where the base image ships `cmake`/`ninja`/`g++` but no Qt and an
empty Conan cache, so `windows-default` and `linux-backend-only` cannot even
configure (both require Qt6 `Core Gui SerialPort Network`). Provisioning Qt6
offline is unreliable there. The Qt-free `mib_processing` core builds against
system OpenCV + HDF5 + spdlog alone:

```bash
sudo apt-get update      # the base image apt index is stale — 404s without this
sudo apt-get install -y --no-install-recommends \
    libopencv-dev libhdf5-dev libspdlog-dev libfmt-dev nlohmann-json3-dev

cmake -S . -B build/proc-only -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DMIB_BUILD_PROCESSING_ONLY=ON -DMIB_BUILD_BACKEND_ONLY=ON \
    -DBUILD_TESTING=OFF -DMIB_USE_SENTRY=OFF   # BUILD_TESTING must be OFF: tests/ links Qt6
cmake --build build/proc-only --target mib_processing
```

- **Covers** (compiles without Qt): `ProcessingService`, `ProcessingScience`,
  `FrameStore`, `Hdf5Service`, `PipelineTimingRecorder`, `CrashStateMirror`,
  `Tools`, `EModulusLut`, the processing kernels.
- **Not covered** (need Qt — rely on `backend-ci.yml`): `TriggerService`,
  `CaptureService`, `AutofocusService`, everything under `src/frontend/`, and
  the whole CTest suite (test targets link `mib_backend`).
- To exercise a pure core function without Qt, link a throwaway `main()`
  against `build/proc-only/libmib_processing.a` with `pkg-config --cflags
  --libs opencv4`, `-lspdlog -lfmt`, and the HDF5 serial libs
  (`-lhdf5_serial -lhdf5_serial_cpp`). Anything needing Qt classes goes through
  CI. When Qt6 *is* available, prefer the `linux-system-release` /
  `linux-backend-only` presets above instead.

## Compile Sentry Integration

Use this when touching `CrashReporter` code and you want local compile coverage
for the sentry-native API calls:

```bash
conan install . --profile:host=conan/profiles/linux-gcc13 --profile:build=default --output-folder=build/linux-sentry --build=missing
cmake --preset linux-sentry-release
cmake --build --preset linux-sentry-release-build
```

This keeps Windows SDKs and packaging disabled but enables `MIB_USE_SENTRY`.
If sentry-native or system curl dependencies are unavailable, use the regular
`linux-release` preset for the quick compile loop and keep the Windows workflow
for final release packaging.
