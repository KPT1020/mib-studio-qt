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


## Kill-style HDF5 durability validation

Build the abrupt-stop validator in backend-only mode:

```bash
cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build --target hdf5_abrupt_stop_tool
```

Then run a writer mode, SIGKILL it from another shell, and validate:

```bash
# shell A
build/linux-backend/hdf5_abrupt_stop_tool run-experiment /tmp/kill_exp.h5

# shell B
pkill -9 -f "hdf5_abrupt_stop_tool run-experiment /tmp/kill_exp.h5"
build/linux-backend/hdf5_abrupt_stop_tool check-checkpoint /tmp/kill_exp.h5
build/linux-backend/hdf5_abrupt_stop_tool check-experiment /tmp/kill_exp.h5

# recording mode check
build/linux-backend/hdf5_abrupt_stop_tool check-recording /tmp/kill_rec.h5
```

`check-experiment` and `check-recording` emit `<file>.preview.png` side-by-side
input-vs-saved previews and enforce bit-identical pixel comparison between each
saved frame and its corresponding mock input frame (by metadata index).
