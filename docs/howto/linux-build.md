# Linux Build

Use this path for fast local compile verification when changing portable app
code. It deliberately disables Windows-only hardware SDKs and installer
packaging, so it does not require EGrabber, Coremor, MSVC, windeployqt, or
InnoSetup.

## Prerequisites

- CMake 3.21+
- Conan 2.x
- A C++17 compiler toolchain
- `make`

## Configure And Build

```bash
conan install . --output-folder=build/linux --build=missing -s build_type=Release
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
conan install . --output-folder=build/linux-sentry --build=missing -s build_type=Release
cmake --preset linux-sentry-release
cmake --build --preset linux-sentry-release-build
```

This keeps Windows SDKs and packaging disabled but enables `MIB_USE_SENTRY`.
If sentry-native or system curl dependencies are unavailable, use the regular
`linux-release` preset for the quick compile loop and keep the Windows workflow
for final release packaging.
