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

## Cloud agent / fresh container setup

A freshly cloned cloud container ships `cmake`/`ninja`/`g++` but **no Qt, no
OpenCV/HDF5/spdlog, and an empty Conan cache**, so none of the presets above
configure until you provision system packages. Two paths, fastest first.

First, always refresh apt — the base image index is stale and 404s otherwise:

```bash
sudo apt-get update
```

**Fastest loop — Qt-free processing core.** The `mib_processing` static lib
(portable processing contract) needs only OpenCV + HDF5 + spdlog, no Qt:

```bash
sudo apt-get install -y --no-install-recommends \
    libopencv-dev libhdf5-dev libspdlog-dev libfmt-dev nlohmann-json3-dev

cmake -S . -B build/proc-only -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DMIB_BUILD_PROCESSING_ONLY=ON -DMIB_BUILD_BACKEND_ONLY=ON \
    -DBUILD_TESTING=OFF -DMIB_USE_SENTRY=OFF   # BUILD_TESTING must be OFF here: tests/ links Qt
cmake --build build/proc-only --target mib_processing
```

Covers `ProcessingService`, `ProcessingScience`, `FrameStore`, `Hdf5Service`,
`PipelineTimingRecorder`, `CrashStateMirror`, `Tools`, `EModulusLut`, the
kernels. To exercise a pure core function, link a throwaway `main()` against
`build/proc-only/libmib_processing.a` with `pkg-config --cflags --libs
opencv4`, `-lspdlog -lfmt`, and the HDF5 serial libs (`-lhdf5_serial
-lhdf5_serial_cpp`). Does **not** cover `TriggerService`, `CaptureService`,
`AutofocusService`, `src/frontend/`, or the CTest suite (all link Qt).

**Full backend + tests — install Qt from apt.** Qt6 provisions cleanly from
Ubuntu packages (Noble ships Qt **6.4.2**, older than the pinned 6.7.3 but it
configures, builds `mib_backend` + `mib_frontend_common`, and passes the whole
`linux-backend-only` CTest suite):

```bash
sudo apt-get install -y --no-install-recommends \
    qt6-base-dev qt6-charts-dev qt6-serialport-dev libsqlite3-dev \
    libopencv-dev libhdf5-dev libspdlog-dev libfmt-dev nlohmann-json3-dev

cmake --preset linux-backend-only          # or linux-system-release for the frontend libs
cmake --build --preset linux-backend-only-build
ctest --preset linux-backend-only-test     # `pip install numpy` for the conformance script test
```

Prefer this whenever you touch Qt-dependent code so it is locally verified
rather than left to `backend-ci.yml`.

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
