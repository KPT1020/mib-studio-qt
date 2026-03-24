# -*- mode: python ; coding: utf-8 -*-
"""
PyInstaller spec file for HDF5 Export GUI Application

Build commands:
    Windows: pyinstaller hdf5_export.spec
    Mac: pyinstaller hdf5_export.spec

Note: This spec file should be run from the scripts/ directory.
"""

import sys
import os
from pathlib import Path

block_cipher = None

# Use current working directory - build script changes to scripts/ before running PyInstaller
# SPECPATH may resolve incorrectly, so we rely on cwd which is guaranteed to be correct
spec_dir = Path.cwd().resolve()

# Script file name (should be in same directory as spec file)
script_name = 'hdf5_export_app.py'

# Construct absolute path to script
script_path = spec_dir / script_name

# Verify script exists
if not script_path.exists():
    raise FileNotFoundError(
        f"Script file not found: {script_name}\n"
        f"Looked in: {spec_dir}\n"
        f"Current working directory: {Path.cwd()}\n"
        f"Files in directory: {list(spec_dir.glob('*.py'))}"
    )

a = Analysis(
    [str(script_path)],
    pathex=[str(spec_dir)],
    binaries=[],
    datas=[],
    hiddenimports=[
        'h5py',
        'h5py._hl',
        'h5py.h5ac',
        'numpy',
        'numpy.core._methods',
        'numpy.lib.format',
        'cv2',
        'PySide6.QtCore',
        'PySide6.QtGui',
        'PySide6.QtWidgets',
        'export_hdf5',
        'export_worker',
    ],
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

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='hdf5_export_app',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,  # No console window for GUI app
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=None,  # Can add icon file path here if available
)
