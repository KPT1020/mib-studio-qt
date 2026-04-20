# mib-studio-qt

## Documentation
See `docs/README.md` for structure and links. Active tasks live in `knowledge_map/task/`.


## Algorithm Experiments & MLflow

When running algorithm experiments (reanalysis, parameter sweeps, pipeline comparisons), all intermediate images and results must be uploaded to the MLflow tracking server at `mlflow.yofo.bio`. This includes intermediate pipeline images (original, blurred, diff, thresh, mask, overlay), metrics CSV, processing parameters, and summary metrics. See `CLAUDE.md` for detailed integration instructions and example code.

## Post-processing tools

Standalone tools for working with HDF5 files after recording (export, reanalyse) are built and distributed separately. See [docs/howto/tools.md](docs/howto/tools.md) for download location, quickstart, and compatibility.

## Building

### Prerequisites

1. **Conan** - Install Conan 2.x if not already installed:
   ```bash
   pip install conan
   ```

2. **CMake** - Version 3.21 or higher

3. **C++ Compiler** - MSVC (Visual Studio) on Windows

### Setup

1. **Install Conan dependencies**:
   ```bash
   conan install . -of build --build=missing -s build_type=Release
   ```
   
   For Debug builds, also install Debug packages:
   ```bash
   conan install . -of build --build=missing -s build_type=Debug
   ```

2. **Configure CMake using preset**:
   ```bash
   cmake --preset windows-default
   ```

3. **Build native targets** (from repo root):

   **Release** (recommended for installers and day-to-day use):
   ```bash
   cmake --build build --preset windows-default-build-release
   ```

   **Debug**:
   ```bash
   cmake --build build --config Debug
   ```

   Outputs land under `build/Release/` or `build/Debug/` (e.g. `mib_studio_qt.exe`, `mock_studio_qt.exe`). For Qt Release runs, deploy Qt DLLs (CMake may run this post-build; otherwise run `windeployqt.exe --release build/Release/mib_studio_qt.exe`).

### Tauri desktop shell (`src-tauri/`)

Requires **Node.js** (for `npm`) and a **Rust** toolchain (`rustup`) with `cargo`. The app links the static **`mib_backend`** library and a cxx bridge; it does **not** replace the CMake configure step—Conan must still populate `build/` so `build.rs` can read `*-release-x86_64-data.cmake` and find `mib_backend.lib`.

**Workflow:**

1. Complete **Prerequisites** and **Setup** above (`conan install`, `cmake --preset windows-default`).
2. Build the backend library (Release):
   ```bash
   cmake --build build --preset windows-default-build-release --target mib_backend
   ```
3. **Runtime DLLs (Windows):** `mib-studio.exe` loads OpenCV, HDF5, ONNX Runtime, etc. **`src-tauri/build.rs` copies Conan `bin\\*.dll` and `XMT_DLL_SER.dll` into `src-tauri/target/<profile>/` after linking**, so a plain `cargo run --release` usually works without setting `PATH`. If you still see **`0xC0000135`**, run `cargo build` again (or use Conan’s run env below). The Qt app still needs Qt on `PATH`; the Tauri binary does **not** need Qt DLLs for `mib_backend`.
   - Optional: after `conan install`, Conan’s **VirtualRunEnv** can provide `build/conanrun.bat` for a PATH-only workflow:
   ```powershell
   .\scripts\run-tauri-with-conan-path.ps1
   ```
   Or: `cmd /c "call build\conanrun.bat && cd src-tauri && cargo run --release"`.
4. Build or run the Tauri app from the **repository root** (where `package.json` lives):
   ```bash
   npm install
   npm run tauri:dev
   ```
   (Ensure `PATH` includes Conan bins as in step 3, or use `run-tauri-with-conan-path.ps1` / `conanrun.bat` when invoking `cargo`.)
   Or build the binary only:
   ```bash
   cd src-tauri
   cargo build --release
   ```

**After changing C++ ABI used by the bridge** (e.g. `CaptureService::FrameCallback`), rebuild `mib_backend` before `cargo build`.

More detail: [docs/howto/tauri-backend-bridge.md](docs/howto/tauri-backend-bridge.md). Mock camera env vars: see `CLAUDE.md` / **Running**.

### Windows installer (Qt app)

To create a Windows installer for distribution, see [docs/howto/build-installer.md](docs/howto/build-installer.md).

Quick start:

1. Build Release: `cmake --build build --config Release` (or the Release preset above).
2. Build installer: `cmake --build build --target package_installer`
3. Build update package: `cmake --build build --target package_installer_update`
4. Find installer at: `build/dist/MIB_Studio_Qt_Setup_v0.1.0.exe` (version in filename may differ).

After building installers, see [Publishing Updates](#publishing-updates) below to publish them for distribution.

## Publishing Updates

After building installers, use `publish-update.ps1` to upload them to RustFS (S3-compatible storage) for distribution.

**Prerequisites:**
- AWS CLI installed and configured
- RustFS credentials (via profile or environment variables)

**Publish update package (for auto-updates):**
```powershell
.\publish-update.ps1 -Installer "build\dist\MIB_Studio_Qt_Update_v0.2.0.exe" -Profile rustfs
```

**Publish full installer (optional, for manual downloads):**
```powershell
.\publish-update.ps1 -Installer "build\dist\MIB_Studio_Qt_Setup_v0.2.0.exe" -Profile rustfs
```

The script auto-detects version from filename, computes SHA-256, generates a manifest, and uploads files with public-read ACL.

For complete release workflow, see [docs/howto/release-workflow.md](docs/howto/release-workflow.md).  
For detailed publishing information, see [docs/howto/auto-update-rustfs.md](docs/howto/auto-update-rustfs.md).

## Version Management

The project uses semantic versioning (X.Y.Z). Version can be managed in two ways:

> **For complete release workflow** (version bump → build → publish), see [docs/howto/release-workflow.md](docs/howto/release-workflow.md).

### Manual Version Bumping

Use the `bump-version.ps1` script to increment the version:

```powershell
# Bump patch version (0.1.0 → 0.1.1)
.\bump-version.ps1 --patch

# Bump minor version (0.1.0 → 0.2.0)
.\bump-version.ps1 --minor

# Bump major version (0.1.0 → 1.0.0)
.\bump-version.ps1 --major

# Bump and create git tag automatically
.\bump-version.ps1 --patch --tag
```

The script updates `CMakeLists.txt` and optionally creates a git tag (e.g., `v0.1.1`). If you create a tag, remember to push it:
```bash
git push origin v0.1.1
```

### Automatic Version from Git Tags

CMake automatically detects version from git tags during configuration. If a git tag matching the pattern `vX.Y.Z` or `X.Y.Z` exists and is newer than the hardcoded version in `CMakeLists.txt`, it will be used. This allows versioning based on git tags while maintaining a fallback default version.

The version is used throughout the build:
- Application version (shown in About dialog and used by auto-updater)
- Installer filename and metadata
- All build artifacts