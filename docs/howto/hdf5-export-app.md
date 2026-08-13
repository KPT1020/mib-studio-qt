# HDF5 Export GUI Application

This guide explains how to build and use the standalone HDF5 Export GUI application, which provides a user-friendly interface for exporting metrics and images from MIB Studio HDF5 files.

**User download:** For pre-built executables and the full tools bundle, see [tools.md](tools.md).

## Overview

The HDF5 Export GUI Application is a standalone PySide6 (Qt for Python) application that wraps the functionality of `scripts/export_hdf5.py` in a graphical user interface. It can be packaged as a standalone executable for Windows (.exe), macOS (.app/.dmg), and Linux (ELF executable).

## Prerequisites

### Windows

- Python 3.8 or later
- PowerShell 5.1 or later

### macOS

- Python 3.8 or later
- Xcode Command Line Tools (for building)
- `hdiutil` (included with macOS, for creating DMG files)

### Linux

- Python 3.8 or later
- `pip` available for your Python installation
- Optional but recommended: `python3-venv` package (if you want isolated venv builds)

## Building the Application

### Windows

1. **Navigate to the scripts directory:**
   ```powershell
   cd scripts
   ```

2. **Run the build script:**
   ```powershell
   .\build_windows.ps1
   ```

   To clean previous builds first:
   ```powershell
   .\build_windows.ps1 -Clean
   ```

3. **Find the executable:**
   The built executable will be located at:
   ```
   scripts\dist\hdf5_export_app.exe
   ```

### macOS

1. **Run the exporter-only build from the repository root:**
   ```bash
   ./tools/build_mac.sh --clean --dmg --exporter-only
   ```

2. **Find the application bundle and disk image:**
   The built application will be located at:
   ```
   tools/dist/hdf5_export_app.app
   ```

   The native disk image is named for the build machine's architecture:
   ```
   tools/dist/MIB_HDF5_Exporter_macos_arm64.dmg
   tools/dist/MIB_HDF5_Exporter_macos_x86_64.dmg
   ```

The `macOS HDF5 Exporter` GitHub Actions workflow builds both variants and
checks the bundle signature, executable architecture, property list, and GUI
startup before uploading them as workflow artifacts.

### Linux

1. **Navigate to the scripts directory:**
   ```bash
   cd scripts
   ```

2. **Run the Unix build script:**
   ```bash
   ./build_mac.sh
   ```

   To clean previous builds:
   ```bash
   ./build_mac.sh --clean
   ```

3. **Find the executable:**
   The built executable will be located at:
   ```
   scripts/dist/hdf5_export_app
   ```

## Manual Build Process

If you prefer to build manually:

### 1. Set Up Virtual Environment

**Windows:**
```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
```

**macOS / Linux:**
```bash
python3 -m venv .venv
source .venv/bin/activate
```

### 2. Install Dependencies

```bash
pip install --upgrade pip
pip install -r requirements.txt
```

### 3. Build with PyInstaller

```bash
pyinstaller hdf5_export.spec --clean
```

The executable will be in the `dist` directory.

## Using the Application

### Launching the Application

**Windows:**
Double-click `hdf5_export_app.exe` or run from command line:
```powershell
.\dist\hdf5_export_app.exe
```

**macOS:**
Double-click `hdf5_export_app.app` in Finder, or run from command line:
```bash
open tools/dist/hdf5_export_app.app
```

**Linux:**
Run from command line:
```bash
./dist/hdf5_export_app
```

### Using the GUI

1. **Select Input File:**
   - Click "Browse..." next to "Input File"
   - Select your HDF5 file (.h5 or .hdf5)

2. **Select Output Directory:**
   - Click "Browse..." next to "Output Directory"
   - Choose where you want the exported files to be saved

3. **Configure Export Options:**
   - **Format:** Choose "CSV (metrics only)", "Images only", or "All (CSV + Images)"
   - **Frame Type:** Choose "Both", "Valid only", or "Invalid only"
   - **Pixel to Micron:** Enter the conversion factor (default: 0.4886)

4. **Export:**
   - Click the "Export" button
   - Monitor progress in the status area
   - The progress bar shows export completion
   - Click "Cancel" to abort if needed

5. **Completion:**
   - A message box will appear when export completes
   - Check the output directory for exported files. Generated names are derived from the selected HDF5 filename and avoid existing files/folders by appending `_2`, `_3`, and so on:
     - `<input-basename>_metrics.csv` for CSV-only export
     - `<input-basename>/metrics.csv` for All export
     - `<input-basename>/valid_frame_XXXXXX.tiff` and `<input-basename>/invalid_frame_XXXXXX.tiff` for image exports

## Output Files

### CSV Export

CSV-only exports write `<input-basename>_metrics.csv` under the selected output directory. All exports write `metrics.csv` inside a source-specific `<input-basename>/` folder. Existing generated names are not overwritten; the exporter appends `_2`, `_3`, and so on before writing.

The CSV file contains the following columns:
- Frame Type (Valid/Invalid)
- Index
- Timestamp
- Deformability
- Area
- Area (um²)
- Area Ratio
- Ring Ratio
- Valid (Yes/No)
- Touches Border (Yes/No)
- Single Inner (Yes/No)
- In Range (Yes/No)
- Inner Count
- Bright Q1, Q2, Q3, Q4

### Image Export

Images are exported into a source-specific folder (`<input-basename>/`, suffixed if needed) as TIFF files with the naming pattern:
- `valid_frame_XXXXXX.tiff` for valid frames
- `invalid_frame_XXXXXX.tiff` for invalid frames

Where `XXXXXX` is the zero-padded frame index.

## Troubleshooting

### Build Issues

**"Python not found"**
- Ensure Python 3.8+ is installed and in your PATH
- On Windows, you may need to restart PowerShell after installing Python

**"PyInstaller not found"**
- Ensure you've activated the virtual environment
- Run `pip install -r requirements.txt` again
- If you are using system Python (no venv), run:
  `python3 -m pip install --user pyinstaller`

**"Module not found" errors during build**
- Check that all dependencies in `requirements.txt` are installed
- Try cleaning and rebuilding: `build_windows.ps1 -Clean` or `./build_mac.sh --clean`
- On Linux system Python, install user-scoped deps:
  `python3 -m pip install --user -r requirements.txt`

### Runtime Issues

**"Failed to open HDF5 file"**
- Verify the HDF5 file path is correct
- Check that the file is not corrupted
- Ensure you have read permissions for the file

**"opencv-python (cv2) is required for image export"**
- This error appears if you try to export images but OpenCV is not available
- The bundled executable should include OpenCV, but if this error occurs, rebuild the application

**Application won't start**
- On Windows, check Windows Defender or antivirus isn't blocking the executable
- The macOS internal build is ad-hoc signed but not Apple notarized. On first
  launch, Control-click the app and choose **Open**. If macOS still blocks it,
  review the message under System Settings > Privacy & Security.
- On Linux, ensure required Qt/X11 runtime libraries are installed (for example `libxcb-cursor0` and TIFF runtime libs, depending on distro)
- Try running from command line to see error messages

**Large file sizes**
- The bundled executable includes Python runtime and all dependencies
- Typical sizes: 100-200 MB for Windows .exe, 150-300 MB for macOS .app
- This is normal for PyInstaller bundles

## Command-Line Alternative

The original command-line script (`export_hdf5.py`) remains available for automated use:

```bash
python scripts/export_hdf5.py -i experiment.h5 -o ./export --format all
```

`--output` / `-o` is always an output directory. It is required, created when missing, and must not be an existing file or a path ending in `.csv`; explicit output-file mode is not supported. CSV-only exports create `<input-basename>_metrics.csv` under that directory, while image/all exports create a collision-safe `<input-basename>/` folder.

See `scripts/export_hdf5.py --help` for all options.

## Related

- **Reanalysis tool:** To re-run the processing pipeline on an existing .h5 and save all intermediate images (blurred, diff, threshold, mask) for analysis, see [reanalyse-hdf5.md](reanalyse-hdf5.md).
- **All tools and downloads:** [tools.md](tools.md).

## Distribution

### Windows

The `.exe` file is standalone and can be distributed directly. Users don't need Python installed.

### macOS

Build the native architecture-specific DMG from the repository root:
```bash
./tools/build_mac.sh --clean --dmg --exporter-only
```

Use the `arm64` artifact for Apple Silicon and `x86_64` for Intel. The build
also writes a `.sha256` checksum beside the DMG. Public distribution without a
Gatekeeper warning requires an Apple Developer ID signing certificate and
notarization; the automated internal build currently uses PyInstaller's ad-hoc
signature.

### Linux

The `hdf5_export_app` binary is a standalone Linux executable produced by PyInstaller. It is not a `.deb`/`.rpm` package; distribute the binary together with any distro runtime dependency instructions your users need.

## Technical Details

### Dependencies

- **PySide6**: Qt for Python GUI framework
- **h5py**: HDF5 file reading
- **numpy**: Array handling
- **opencv-python**: Image export functionality
- **PyInstaller**: Application packaging

### Architecture

- `hdf5_export_app.py`: Main GUI application
- `export_worker.py`: Background worker for non-blocking export operations
- `export_hdf5.py`: Core export logic (shared with CLI version)

The GUI uses Qt signals and slots for communication between the main thread and the worker thread, ensuring the UI remains responsive during export operations.
