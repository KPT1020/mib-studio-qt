#!/usr/bin/env python3
"""Tests for scripts/convert_legacy_csv_to_json.py against the gold-standard contract."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import convert_legacy_csv_to_json as convert_mod  # noqa: E402

REQUIRED_FRAME_KEYS = {
    "frame_type", "index", "timestamp_ns", "object_id", "object_count",
    "deformability", "area", "area_um2", "area_ratio", "ring_ratio",
    "is_valid", "touches_border", "has_single_inner_contour", "in_range",
    "inner_contour_count", "brightness_q1", "brightness_q2", "brightness_q3", "brightness_q4",
}

LEGACY_CSV = (
    "Batch,Condition,ImageIndex,Timestamp_us,Deformability,Area,RingRatio,Valid,Method,ProcessingConfig\n"
    "1,PANC1,0,1000,0.12,150.5,20.1,1,legacy,cfg_v1\n"
    "1,PANC1,1,2000,0.45,80.2,18.4,0,legacy,cfg_v1\n"
)


class ConvertLegacyCsvToJsonTest(unittest.TestCase):
    def _run_convert(self, csv_text: str, extra_args: list[str] | None = None) -> dict:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            csv_path = root / "legacy.csv"
            csv_path.write_text(csv_text, encoding="utf-8")
            json_path = root / "gold.json"

            argv = ["-i", str(csv_path), "-o", str(json_path)] + (extra_args or [])
            self.assertEqual(convert_mod.main(argv), 0)
            return json.loads(json_path.read_text(encoding="utf-8"))

    def test_conversion_matches_gold_standard_contract(self) -> None:
        document = self._run_convert(LEGACY_CSV)

        self.assertEqual(document["version"], 1)
        self.assertAlmostEqual(document["pixel_to_micron"], 0.4886)
        self.assertEqual(document["source"], "MIB-Studio-gold:1:PANC1")
        self.assertEqual(len(document["frames"]), 2)

        for frame in document["frames"]:
            self.assertEqual(set(frame.keys()), REQUIRED_FRAME_KEYS)
            self.assertIn(frame["frame_type"], ("valid", "invalid"))

        valid_frame, invalid_frame = document["frames"]
        self.assertEqual(valid_frame["frame_type"], "valid")
        self.assertTrue(valid_frame["is_valid"])
        self.assertEqual(valid_frame["index"], 0)
        self.assertEqual(valid_frame["timestamp_ns"], 1000000)  # Timestamp_us * 1000
        self.assertAlmostEqual(valid_frame["area_um2"], 150.5 * 0.4886 * 0.4886)

        self.assertEqual(invalid_frame["frame_type"], "invalid")
        self.assertFalse(invalid_frame["is_valid"])

    def test_explicit_source_overrides_batch_condition(self) -> None:
        document = self._run_convert(LEGACY_CSV, ["--source", "custom-source"])
        self.assertEqual(document["source"], "custom-source")

    def test_missing_required_column_errors(self) -> None:
        csv_text = "ImageIndex,Deformability,RingRatio,Valid\n0,0.1,20.0,1\n"
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            csv_path = root / "legacy.csv"
            csv_path.write_text(csv_text, encoding="utf-8")
            json_path = root / "gold.json"
            self.assertEqual(convert_mod.main(["-i", str(csv_path), "-o", str(json_path)]), 1)
            self.assertFalse(json_path.exists())

    def test_custom_column_names(self) -> None:
        csv_text = "FrameIndex,Deform,AreaPx,IsValid\n5,0.2,99.0,true\n"
        document = self._run_convert(
            csv_text,
            [
                "--index-column", "FrameIndex",
                "--deformability-column", "Deform",
                "--area-column", "AreaPx",
                "--valid-column", "IsValid",
            ],
        )
        self.assertEqual(document["frames"][0]["index"], 5)
        self.assertEqual(document["frames"][0]["timestamp_ns"], 0)  # no timestamp column -> defaulted


if __name__ == "__main__":
    unittest.main()
