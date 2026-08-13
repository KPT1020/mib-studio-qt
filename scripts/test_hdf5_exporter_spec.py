#!/usr/bin/env python3
"""Regression tests for the standalone HDF5 exporter PyInstaller spec."""

from __future__ import annotations

import os
import runpy
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[1]
SPEC_PATH = REPO_ROOT / "tools" / "hdf5_export_app" / "hdf5_export.spec"


class _Analysis:
    def __init__(self, scripts, hiddenimports=None, **_kwargs):
        self.scripts = scripts
        self.pure = []
        self.zipped_data = []
        self.binaries = []
        self.datas = []
        self.hiddenimports = list(hiddenimports or [])


class Hdf5ExporterSpecTest(unittest.TestCase):
    def execute_spec(self, platform: str):
        calls = {"exe": [], "collect": [], "bundle": []}

        def record(name):
            def factory(*args, **kwargs):
                result = object()
                calls[name].append((args, kwargs, result))
                return result

            return factory

        globals_for_spec = {
            "SPECPATH": str(SPEC_PATH.parent),
            "Analysis": _Analysis,
            "PYZ": lambda *_args, **_kwargs: object(),
            "EXE": record("exe"),
            "COLLECT": record("collect"),
            "BUNDLE": record("bundle"),
        }

        with TemporaryDirectory() as temporary_directory:
            previous_cwd = Path.cwd()
            os.chdir(temporary_directory)
            try:
                with (
                    mock.patch.object(sys, "platform", platform),
                    mock.patch("importlib.util.find_spec", return_value=None),
                ):
                    runpy.run_path(str(SPEC_PATH), init_globals=globals_for_spec)
            finally:
                os.chdir(previous_cwd)

        return calls

    def test_macos_build_defines_native_app_bundle(self):
        calls = self.execute_spec("darwin")

        self.assertEqual(len(calls["collect"]), 1)
        self.assertEqual(len(calls["bundle"]), 1)
        _, bundle_options, _ = calls["bundle"][0]
        self.assertEqual(bundle_options["name"], "hdf5_export_app.app")
        self.assertEqual(bundle_options["bundle_identifier"], "bio.yofo.mib-studio.hdf5-exporter")

    def test_non_macos_build_stays_single_file(self):
        calls = self.execute_spec("win32")

        self.assertEqual(len(calls["collect"]), 0)
        self.assertEqual(len(calls["bundle"]), 0)
        _, exe_options, _ = calls["exe"][0]
        self.assertFalse(exe_options["exclude_binaries"])


if __name__ == "__main__":
    unittest.main()
