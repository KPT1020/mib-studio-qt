#!/usr/bin/env python3
"""Tests for publish-processing-core.py's pure logic (no network/R2 access)."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_PATH = Path(__file__).resolve().parent / "publish-processing-core.py"
_spec = importlib.util.spec_from_file_location("publish_processing_core", SCRIPT_PATH)
publish_processing_core = importlib.util.module_from_spec(_spec)
sys.modules["publish_processing_core"] = publish_processing_core
_spec.loader.exec_module(publish_processing_core)


class BuildWheelEntriesTest(unittest.TestCase):
    def test_parses_platform_tag_from_wheel_filename(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            wheel = root / "mib_processing-0.1.0-cp311-cp311-linux_x86_64.whl"
            wheel.write_text("fake wheel bytes", encoding="utf-8")

            entries = publish_processing_core.build_wheel_entries(
                [wheel], repo="OWNER/REPO", release_tag="mib-processing-v0.1.0"
            )

            self.assertEqual(len(entries), 1)
            entry = entries[0]
            self.assertEqual(entry["platform_tag"], "cp311-cp311-linux_x86_64")
            self.assertEqual(
                entry["url"],
                "https://github.com/OWNER/REPO/releases/download/mib-processing-v0.1.0/"
                "mib_processing-0.1.0-cp311-cp311-linux_x86_64.whl",
            )
            self.assertEqual(len(entry["sha256"]), 64)
            self.assertEqual(entry["sha256"], publish_processing_core.sha256_file(wheel))

    def test_missing_wheel_file_raises(self) -> None:
        with self.assertRaises(ValueError):
            publish_processing_core.build_wheel_entries(
                [Path("/nonexistent/does-not-exist.whl")], repo="OWNER/REPO", release_tag="v1"
            )


class BuildManifestTest(unittest.TestCase):
    def test_manifest_shape_matches_documented_schema(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            wheel = root / "mib_processing-0.2.0-cp312-cp312-linux_x86_64.whl"
            wheel.write_text("fake wheel bytes", encoding="utf-8")

            manifest = publish_processing_core.build_manifest(
                channel="stable",
                contract_version=1,
                wheel_version="0.2.0",
                release_tag="mib-processing-v0.2.0",
                repo="KPT1020/mib-studio-qt",
                wheel_paths=[wheel],
                public_base_url="https://updates.yofo.bio",
            )

            self.assertEqual(manifest["processing_core_manifest_schema_version"], 1)
            self.assertEqual(manifest["channel"], "stable")
            self.assertEqual(manifest["contract_version"], 1)
            self.assertEqual(manifest["wheel"]["package"], "mib-processing")
            self.assertEqual(manifest["wheel"]["version"], "0.2.0")
            self.assertEqual(len(manifest["wheel"]["wheels"]), 1)
            self.assertEqual(
                manifest["profile_catalog_url"], "https://updates.yofo.bio/profiles/stable/catalog.json"
            )
            self.assertEqual(
                manifest["emodulus_lut_manifest_url"],
                "https://updates.yofo.bio/stable/emodulus-lut/latest.json",
            )
            self.assertIn("published_at", manifest)


class ReadWheelVersionTest(unittest.TestCase):
    def test_reads_version_from_real_pyproject(self) -> None:
        pyproject = Path(__file__).resolve().parent / "bindings" / "python" / "pyproject.toml"
        version = publish_processing_core.read_wheel_version(pyproject)
        self.assertRegex(version, r"^\d+\.\d+\.\d+")

    def test_missing_project_version_raises(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            bad_pyproject = Path(temp_dir) / "pyproject.toml"
            bad_pyproject.write_text("[build-system]\nrequires = []\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                publish_processing_core.read_wheel_version(bad_pyproject)


if __name__ == "__main__":
    unittest.main()
