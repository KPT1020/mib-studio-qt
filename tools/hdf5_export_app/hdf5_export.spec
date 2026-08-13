# -*- mode: python ; coding: utf-8 -*-
"""
PyInstaller spec for HDF5 Export GUI (MIB Studio Tools).

Run from tools/ with: pyinstaller hdf5_export_app/hdf5_export.spec
Or from tools/hdf5_export_app/ with: pyinstaller hdf5_export.spec

Script and core modules live in repo scripts/; this spec points at them.
"""

import importlib.util
import sys
from pathlib import Path

block_cipher = None

# Resolve the spec directory independently of the caller's working directory.
# PyInstaller may omit __file__ while executing a spec, but exposes SPECPATH.
if "__file__" in globals():
    spec_dir = Path(__file__).resolve().parent
elif "SPECPATH" in globals():
    spec_dir = Path(SPECPATH).resolve()
else:
    spec_dir = Path.cwd().resolve()

repo_root = spec_dir.parent.parent
scripts_dir = repo_root / "scripts"

script_path = scripts_dir / "hdf5_export_app.py"
if not script_path.exists():
    raise FileNotFoundError(
        f"Script not found: {script_path}\n"
        f"Repo root: {repo_root}\n"
        f"Scripts dir: {scripts_dir}"
    )

hiddenimports = [
    "h5py",
    "h5py._hl",
    "h5py.h5ac",
    "numpy",
    "numpy.lib.format",
    "cv2",
    "PySide6.QtCore",
    "PySide6.QtGui",
    "PySide6.QtWidgets",
    "export_hdf5",
    "export_worker",
]

# NumPy changed its internal package layout between 1.x and 2.x.
for module_name in ("numpy.core._methods", "numpy._core._methods"):
    if importlib.util.find_spec(module_name) is not None:
        hiddenimports.append(module_name)

a = Analysis(
    [str(script_path)],
    pathex=[str(scripts_dir)],
    binaries=[],
    datas=[],
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

common_exe_options = dict(
    name="hdf5_export_app",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=None,
)

if sys.platform == "darwin":
    # A native macOS .app should use an onedir executable. PyInstaller's
    # onefile/windowed combination re-extracts on every launch and is not
    # suitable for later notarization or App Sandbox signing.
    exe = EXE(
        pyz,
        a.scripts,
        [],
        exclude_binaries=True,
        **common_exe_options,
    )
    collected = COLLECT(
        exe,
        a.binaries,
        a.datas,
        strip=False,
        upx=False,
        name="hdf5_export_app",
    )
    app = BUNDLE(
        collected,
        name="hdf5_export_app.app",
        icon=None,
        bundle_identifier="bio.yofo.mib-studio.hdf5-exporter",
        info_plist={
            "CFBundleDisplayName": "MIB HDF5 Exporter",
            "CFBundleName": "MIB HDF5 Exporter",
            "NSHighResolutionCapable": True,
        },
    )
else:
    # Preserve the existing one-file Windows/Linux artifact layout.
    exe = EXE(
        pyz,
        a.scripts,
        a.binaries,
        a.datas,
        [],
        exclude_binaries=False,
        **common_exe_options,
    )
