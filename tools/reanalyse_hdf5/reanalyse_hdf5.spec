# -*- mode: python ; coding: utf-8 -*-
"""
PyInstaller spec for Reanalyse HDF5 CLI (MIB Studio Tools).

Run from tools/ with: pyinstaller reanalyse_hdf5/reanalyse_hdf5.spec
Or from tools/reanalyse_hdf5/ with: pyinstaller reanalyse_hdf5.spec

Script and export_hdf5 live in repo scripts/; this spec points at them.
Output: mib_reanalyse_hdf5.exe (console app so users see progress).
"""

from pathlib import Path

block_cipher = None

# Build script is run from tools/, so cwd is tools/
tools_dir = Path.cwd().resolve()
repo_root = tools_dir.parent
scripts_dir = repo_root / "scripts"

script_path = scripts_dir / "reanalyse_hdf5.py"
if not script_path.exists():
    raise FileNotFoundError(
        f"Script not found: {script_path}\n"
        f"Repo root: {repo_root}\n"
        f"Scripts dir: {scripts_dir}"
    )

a = Analysis(
    [str(script_path)],
    pathex=[str(scripts_dir)],
    binaries=[],
    datas=[],
    hiddenimports=[
        "h5py",
        "h5py._hl",
        "h5py.h5ac",
        "numpy",
        "numpy.core._methods",
        "numpy.lib.format",
        "cv2",
        "export_hdf5",
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
    name="mib_reanalyse_hdf5",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,  # CLI: show progress and errors
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=None,
)
