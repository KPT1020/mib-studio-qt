from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from synthetic_condition_validation import (  # noqa: E402
    DetectionRecord,
    apply_brightness_contrast,
    summarize_detection_records,
)


def test_apply_brightness_contrast_is_deterministic_uint8() -> None:
    image = np.array([[40, 80], [120, 200]], dtype=np.uint8)

    low = apply_brightness_contrast(image, brightness=0.65, contrast=1.0)
    high = apply_brightness_contrast(image, brightness=1.35, contrast=1.0)
    low_again = apply_brightness_contrast(image, brightness=0.65, contrast=1.0)

    assert low.dtype == np.uint8
    assert np.array_equal(low, low_again)
    assert low.tolist() == [[26, 52], [78, 130]]
    assert high.tolist() == [[54, 108], [162, 255]]


def test_contrast_transform_moves_pixels_around_mean() -> None:
    image = np.array([[40, 80], [120, 200]], dtype=np.uint8)

    low = apply_brightness_contrast(image, brightness=1.0, contrast=0.65)
    high = apply_brightness_contrast(image, brightness=1.0, contrast=1.35)

    assert low.tolist() == [[65, 91], [117, 169]]
    assert high.tolist() == [[16, 70], [124, 232]]


def test_summarize_detection_records_reports_condition_counts_and_parity() -> None:
    records = {
        "baseline": [
            DetectionRecord(0, "baseline", True, 1, 12, 10, 101.0),
            DetectionRecord(1, "baseline", False, 0, 0, 0, 0.0),
        ],
        "brightness_low": [
            DetectionRecord(0, "brightness_low", False, 0, 0, 0, 0.0),
            DetectionRecord(1, "brightness_low", True, 2, 30, 22, 150.0),
        ],
    }

    summary = summarize_detection_records(records)

    assert summary["baseline"]["detection_success_count"] == 1
    assert summary["baseline"]["detection_failure_count"] == 1
    assert summary["brightness_low"]["detection_success_count"] == 1
    assert summary["brightness_low"]["detection_failure_count"] == 1
    assert summary["brightness_low"]["baseline_success_variant_failure"] == 1
    assert summary["brightness_low"]["baseline_failure_variant_success"] == 1
    assert summary["brightness_low"]["changed_from_baseline"] == 2
