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
    DatasetSample,
    TRANSFORMS,
    _representative_sample_cases,
    _representative_sample_index,
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


def test_transform_suite_includes_extreme_review_cases() -> None:
    transforms = {transform.name: transform for transform in TRANSFORMS}

    assert (
        transforms["brightness_extreme_low"].brightness
        < transforms["brightness_low"].brightness
    )
    assert (
        transforms["brightness_extreme_high"].brightness
        > transforms["brightness_high"].brightness
    )
    assert (
        transforms["contrast_extreme_low"].contrast
        < transforms["contrast_low"].contrast
    )
    assert (
        transforms["contrast_extreme_high"].contrast
        > transforms["contrast_high"].contrast
    )


def test_representative_sample_prefers_baseline_detected_cells() -> None:
    samples = [
        DatasetSample(10, np.zeros((2, 2), dtype=np.uint8)),
        DatasetSample(11, np.ones((2, 2), dtype=np.uint8)),
    ]
    records = {
        "baseline": [
            DetectionRecord(10, "baseline", False, 0, 0, 0, 0.0),
            DetectionRecord(11, "baseline", True, 1, 4, 4, 2.0),
        ]
    }

    assert _representative_sample_index(samples, records) == 11


def test_representative_sample_falls_back_to_first_sample() -> None:
    samples = [
        DatasetSample(10, np.zeros((2, 2), dtype=np.uint8)),
        DatasetSample(11, np.ones((2, 2), dtype=np.uint8)),
    ]
    records = {
        "baseline": [
            DetectionRecord(10, "baseline", False, 0, 0, 0, 0.0),
            DetectionRecord(11, "baseline", False, 0, 0, 0, 0.0),
        ]
    }

    assert _representative_sample_index(samples, records) == 10


def test_representative_sample_cases_include_cells_and_failures() -> None:
    samples = [
        DatasetSample(0, np.zeros((2, 2), dtype=np.uint8)),
        DatasetSample(1, np.ones((2, 2), dtype=np.uint8)),
        DatasetSample(4, np.full((2, 2), 2, dtype=np.uint8)),
    ]
    records = {
        "baseline": [
            DetectionRecord(0, "baseline", False, 0, 0, 0, 0.0),
            DetectionRecord(1, "baseline", True, 3, 52, 52, 24.0),
            DetectionRecord(4, "baseline", True, 3, 31, 31, 12.0),
        ],
        "brightness_low": [
            DetectionRecord(0, "brightness_low", False, 0, 0, 0, 0.0),
            DetectionRecord(1, "brightness_low", True, 3, 26, 26, 10.0),
            DetectionRecord(4, "brightness_low", False, 0, 0, 0, 0.0),
        ],
        "contrast_low": [
            DetectionRecord(0, "contrast_low", False, 0, 0, 0, 0.0),
            DetectionRecord(1, "contrast_low", True, 3, 24, 24, 9.0),
            DetectionRecord(4, "contrast_low", False, 0, 0, 0, 0.0),
        ],
        "brightness_extreme_low": [
            DetectionRecord(0, "brightness_extreme_low", False, 0, 0, 0, 0.0),
            DetectionRecord(1, "brightness_extreme_low", False, 0, 0, 0, 0.0),
            DetectionRecord(4, "brightness_extreme_low", False, 0, 0, 0, 0.0),
        ],
        "contrast_extreme_low": [
            DetectionRecord(0, "contrast_extreme_low", False, 0, 0, 0, 0.0),
            DetectionRecord(1, "contrast_extreme_low", False, 0, 0, 0, 0.0),
            DetectionRecord(4, "contrast_extreme_low", False, 0, 0, 0, 0.0),
        ],
    }

    cases = _representative_sample_cases(samples, records)
    cases_by_key = {case["key"]: case for case in cases}

    assert len(cases) >= 3
    assert cases_by_key["empty_baseline_reference"]["sample_index"] == 0
    assert cases_by_key["cell_positive_baseline"]["sample_index"] == 1
    assert cases_by_key["standard_low_condition_drop"]["sample_index"] == 4
    assert cases_by_key["extreme_low_condition_drop"]["sample_index"] == 1
    assert (
        cases_by_key["cell_positive_baseline"]["metrics"]["baseline"]["detected"]
        is True
    )


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
