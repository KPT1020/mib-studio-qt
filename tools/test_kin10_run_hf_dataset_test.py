#!/usr/bin/env python3
"""Regression tests for the Hugging Face Dataset Viewer runner."""

from __future__ import annotations

import io
import pathlib
import tempfile
import unittest
import urllib.error
from unittest import mock

import kin10_run_hf_dataset_test as runner


class Kin10HfDatasetRunnerTest(unittest.TestCase):
    def test_fetch_classifies_exhausted_http_503_as_remote_unavailable(self) -> None:
        error = urllib.error.HTTPError(
            "https://datasets-server.example/is-valid",
            503,
            "Service Temporarily Unavailable",
            {},
            io.BytesIO(),
        )
        with mock.patch.object(runner.urllib.request, "urlopen", side_effect=error), mock.patch.object(
            runner.time, "sleep"
        ):
            with self.assertRaises(runner.RemoteServiceUnavailable):
                runner.fetch_bytes(error.url, retries=2)

    def test_main_skips_only_transient_remote_outage(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.object(
            runner,
            "prepare_manifest",
            side_effect=runner.RemoteServiceUnavailable("HTTP Error 503"),
        ):
            status = runner.main(
                [
                    "--out-dir",
                    temp_dir,
                    "--binary",
                    str(pathlib.Path(temp_dir) / "unused"),
                ]
            )
        self.assertEqual(status, runner.SKIP_EXIT_CODE)

    def test_main_keeps_payload_validation_failure_fatal(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.object(
            runner,
            "prepare_manifest",
            side_effect=RuntimeError("Dataset Viewer payload missing splits"),
        ):
            status = runner.main(
                [
                    "--out-dir",
                    temp_dir,
                    "--binary",
                    str(pathlib.Path(temp_dir) / "unused"),
                ]
            )
        self.assertEqual(status, 1)


if __name__ == "__main__":
    unittest.main()
