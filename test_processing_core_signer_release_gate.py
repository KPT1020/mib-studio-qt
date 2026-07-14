#!/usr/bin/env python3
"""Regression coverage for the production processing-core signer trust gate."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent
OPTIONS_FILE = REPO_ROOT / "cmake" / "MIBOptions.cmake"
PIN_NAME = "MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256"
REQUIRE_NAME = "MIB_REQUIRE_PROCESSING_CORE_SIGNER_SPKI"


class ProcessingCoreSignerCMakeTest(unittest.TestCase):
    def run_options(
        self,
        *,
        pin: str = "",
        required: bool = False,
        expected: str | None = None,
    ) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            script = Path(temporary_directory) / "check.cmake"
            expected_check = ""
            if expected is not None:
                expected_check = f"""
if(NOT \"${{{PIN_NAME}}}\" STREQUAL \"{expected}\")
    message(FATAL_ERROR \"normalized signer pin was '${{{PIN_NAME}}}'\")
endif()
"""
            script.write_text(
                f'include([[{OPTIONS_FILE.as_posix()}]])\n{expected_check}',
                encoding="utf-8",
            )
            return subprocess.run(
                [
                    "cmake",
                    f"-D{PIN_NAME}={pin}",
                    f"-D{REQUIRE_NAME}={'ON' if required else 'OFF'}",
                    "-P",
                    str(script),
                ],
                capture_output=True,
                text=True,
                check=False,
            )

    def test_development_build_may_leave_pin_empty(self) -> None:
        result = self.run_options(expected="")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_production_build_requires_pin(self) -> None:
        result = self.run_options(required=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("production build requires", result.stdout + result.stderr)

    def test_pin_must_be_exactly_64_hex_characters(self) -> None:
        for invalid in ("a" * 63, "a" * 65, "g" * 64, "0x" + "a" * 64):
            with self.subTest(pin=invalid):
                result = self.run_options(pin=invalid)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("exactly 64 hexadecimal", result.stdout + result.stderr)

    def test_valid_pin_is_normalized_to_lowercase(self) -> None:
        result = self.run_options(pin="ABCDEF12" * 8, required=True, expected="abcdef12" * 8)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class ProcessingCoreSignerReleaseWiringTest(unittest.TestCase):
    def read(self, relative_path: str) -> str:
        return (REPO_ROOT / relative_path).read_text(encoding="utf-8")

    def test_desktop_workflows_use_repository_pin_and_required_cmake_gate(self) -> None:
        for workflow in (
            ".github/workflows/release.yml",
            ".github/workflows/build-windows.yml",
        ):
            with self.subTest(workflow=workflow):
                content = self.read(workflow)
                self.assertIn("vars.MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256", content)
                self.assertIn("-DMIB_REQUIRE_PROCESSING_CORE_SIGNER_SPKI=ON", content)
                self.assertIn("-DMIB_PROCESSING_CORE_SIGNER_SPKI_SHA256=", content)

    def test_manual_workflow_validates_before_version_mutation(self) -> None:
        content = self.read(".github/workflows/build-windows.yml")
        self.assertLess(
            content.index("name: Validate processing-core signer trust pin"),
            content.index("name: Determine version"),
        )

    def test_local_release_reads_repo_pin_before_bump_and_reconfigures(self) -> None:
        content = self.read("release.ps1")
        preflight = content[: content.index("# --- Step 1: Bump version ---")]
        self.assertIn("if (-not $SkipBuild) {", preflight)
        self.assertNotIn("if ($Push -and -not $SkipBuild) {", preflight)
        self.assertLess(
            content.index("gh variable get MIB_PROCESSING_CORE_SIGNER_SPKI_SHA256"),
            content.index("# --- Step 1: Bump version ---"),
        )
        self.assertIn("-DMIB_REQUIRE_PROCESSING_CORE_SIGNER_SPKI=ON", content)
        self.assertIn("-DMIB_PROCESSING_CORE_SIGNER_SPKI_SHA256=", content)

    def test_native_release_compares_actual_der_spki_to_repository_pin(self) -> None:
        content = self.read(".github/workflows/python-wheel.yml")
        self.assertIn("ExportSubjectPublicKeyInfo", content)
        self.assertIn("Get-AuthenticodeSignature", content)
        self.assertIn("actualSignerSpki -ne $approvedSignerSpki", content)


if __name__ == "__main__":
    unittest.main()
