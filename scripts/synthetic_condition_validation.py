#!/usr/bin/env python3
"""Validate detection behavior under deterministic brightness/contrast variants.

This harness samples frames from gavinlouuu/512x96stream, generates synthetic
image-condition variants in memory, runs the existing empty-frame detection
processing path, and writes reviewer-facing images plus metrics.
"""

from __future__ import annotations

import argparse
import itertools
import json
import shlex
import shutil
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Callable


DATASET_NAME = "gavinlouuu/512x96stream"
DATASET_CONFIG = "default"
DATASET_SPLIT = "train"
DEFAULT_OUTPUT_DIR = "review_artifacts/KIN-12"
DEFAULT_SAMPLE_START = 0
DEFAULT_SAMPLE_COUNT = 8
DEFAULT_BACKGROUND_SAMPLE_COUNT = 1


@dataclass(frozen=True)
class TransformSpec:
    name: str
    brightness: float = 1.0
    contrast: float = 1.0
    description: str = ""


TRANSFORMS: tuple[TransformSpec, ...] = (
    TransformSpec("baseline", 1.0, 1.0, "Unmodified sampled frame."),
    TransformSpec("brightness_low", 0.65, 1.0, "Brightness scaled to 65%."),
    TransformSpec("brightness_high", 1.35, 1.0, "Brightness scaled to 135%."),
    TransformSpec(
        "brightness_extreme_low",
        0.35,
        1.0,
        "Brightness scaled to 35% for an extreme low-light case.",
    ),
    TransformSpec(
        "brightness_extreme_high",
        1.75,
        1.0,
        "Brightness scaled to 175% for an extreme bright case.",
    ),
    TransformSpec("contrast_low", 1.0, 0.65, "Contrast scaled to 65% around frame mean."),
    TransformSpec("contrast_high", 1.0, 1.35, "Contrast scaled to 135% around frame mean."),
    TransformSpec(
        "contrast_extreme_low",
        1.0,
        0.35,
        "Contrast scaled to 35% around frame mean for an extreme low-contrast case.",
    ),
    TransformSpec(
        "contrast_extreme_high",
        1.0,
        1.75,
        "Contrast scaled to 175% around frame mean for an extreme high-contrast case.",
    ),
)


@dataclass(frozen=True)
class DatasetSample:
    sample_index: int
    image: Any


@dataclass(frozen=True)
class DetectionRecord:
    sample_index: int
    condition: str
    detected: bool
    contour_count: int
    mask_pixels: int
    band_mask_pixels: int
    max_contour_area: float


def apply_brightness_contrast(
    image: Any,
    *,
    brightness: float,
    contrast: float,
) -> Any:
    """Apply deterministic brightness and contrast changes to a uint8 image."""
    import numpy as np

    arr = np.asarray(image)
    adjusted = arr.astype(np.float32)

    if contrast != 1.0:
        axes = (0, 1) if adjusted.ndim == 3 else None
        mean = adjusted.mean(axis=axes, keepdims=True) if axes else adjusted.mean()
        adjusted = (adjusted - mean) * contrast + mean

    if brightness != 1.0:
        adjusted = adjusted * brightness

    return np.clip(np.floor(adjusted + 0.5), 0, 255).astype(np.uint8)


def summarize_detection_records(
    records_by_condition: dict[str, list[DetectionRecord]],
) -> dict[str, dict[str, Any]]:
    """Summarize detection success/failure and parity against baseline."""
    baseline_by_sample = {
        record.sample_index: record.detected
        for record in records_by_condition.get("baseline", [])
    }

    summary: dict[str, dict[str, Any]] = {}
    for condition, records in records_by_condition.items():
        total = len(records)
        successes = sum(1 for record in records if record.detected)
        failures = total - successes

        parity_counts = {
            "baseline_success_variant_success": 0,
            "baseline_success_variant_failure": 0,
            "baseline_failure_variant_success": 0,
            "baseline_failure_variant_failure": 0,
            "same_as_baseline": 0,
            "changed_from_baseline": 0,
        }
        for record in records:
            baseline_detected = baseline_by_sample.get(record.sample_index)
            if baseline_detected is None:
                continue
            if baseline_detected and record.detected:
                parity_counts["baseline_success_variant_success"] += 1
            elif baseline_detected and not record.detected:
                parity_counts["baseline_success_variant_failure"] += 1
            elif not baseline_detected and record.detected:
                parity_counts["baseline_failure_variant_success"] += 1
            else:
                parity_counts["baseline_failure_variant_failure"] += 1

            if baseline_detected == record.detected:
                parity_counts["same_as_baseline"] += 1
            else:
                parity_counts["changed_from_baseline"] += 1

        summary[condition] = {
            "total_frames": total,
            "detection_success_count": successes,
            "detection_failure_count": failures,
            "detection_success_rate": successes / total if total else 0.0,
            **parity_counts,
        }

    return summary


def _processing_module():
    script_dir = Path(__file__).resolve().parent
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))

    from empty_frame_detection import (  # type: ignore
        ProcessingConfig,
        build_background,
        ensure_grayscale,
        process_frame,
    )

    return ProcessingConfig, build_background, ensure_grayscale, process_frame


def _dataset_revision(dataset_name: str) -> str | None:
    try:
        from huggingface_hub import HfApi

        return HfApi().dataset_info(dataset_name).sha
    except Exception:
        return None


def load_samples(
    dataset_name: str,
    dataset_config: str,
    split: str,
    sample_start: int,
    sample_count: int,
) -> tuple[list[DatasetSample], dict[str, Any]]:
    """Load an exact Hugging Face split slice and preserve original row indices."""
    import numpy as np
    from datasets import load_dataset

    if sample_start < 0:
        raise ValueError("--sample-start must be >= 0")
    if sample_count < 1:
        raise ValueError("--sample-count must be >= 1")

    sample_end = sample_start + sample_count
    split_expr = f"{split}[{sample_start}:{sample_end}]"
    if dataset_config:
        dataset = load_dataset(
            dataset_name,
            dataset_config,
            split=split,
            streaming=True,
        )
    else:
        dataset = load_dataset(dataset_name, split=split, streaming=True)

    samples: list[DatasetSample] = []
    selected_rows = itertools.islice(dataset, sample_start, sample_end)
    for offset, sample in enumerate(selected_rows):
        samples.append(
            DatasetSample(
                sample_index=sample_start + offset,
                image=np.asarray(sample["image"]),
            )
        )

    if len(samples) != sample_count:
        raise ValueError(
            f"Requested {sample_count} samples from {split_expr}, got {len(samples)}"
        )

    first_shape = list(samples[0].image.shape) if samples else []
    dataset_info = {
        "name": dataset_name,
        "config": dataset_config,
        "split": split,
        "split_expression": split_expr,
        "sample_start": sample_start,
        "sample_end_exclusive": sample_end,
        "sample_count": sample_count,
        "sample_indices": [sample.sample_index for sample in samples],
        "revision": _dataset_revision(dataset_name),
        "first_sample_shape": first_shape,
    }
    return samples, dataset_info


def _condition_frames(
    samples: list[DatasetSample],
    transform: TransformSpec,
) -> list[Any]:
    if transform.name == "baseline":
        return [sample.image for sample in samples]
    return [
        apply_brightness_contrast(
            sample.image,
            brightness=transform.brightness,
            contrast=transform.contrast,
        )
        for sample in samples
    ]


def run_detection(
    samples: list[DatasetSample],
    config: Any,
    background_mode: str,
    background_sample_count: int,
) -> tuple[
    dict[str, list[Any]],
    dict[str, list[DetectionRecord]],
    dict[str, list[Any]],
]:
    """Run the existing processing path for baseline and each transform."""
    import cv2
    import numpy as np

    _, build_background, ensure_grayscale, process_frame = _processing_module()

    if background_sample_count < 1:
        raise ValueError("--background-sample-count must be >= 1")
    if background_sample_count > len(samples):
        raise ValueError("--background-sample-count cannot exceed --sample-count")

    frames_by_condition = {
        transform.name: _condition_frames(samples, transform)
        for transform in TRANSFORMS
    }
    baseline_background = build_background(
        [
            ensure_grayscale(frame)
            for frame in frames_by_condition["baseline"][:background_sample_count]
        ]
    )

    records_by_condition: dict[str, list[DetectionRecord]] = {}
    results_by_condition: dict[str, list[Any]] = {}

    for transform in TRANSFORMS:
        condition = transform.name
        frames = frames_by_condition[condition]
        if background_mode == "baseline":
            background = baseline_background
        else:
            background = build_background(
                [
                    ensure_grayscale(frame)
                    for frame in frames[:background_sample_count]
                ]
            )

        records: list[DetectionRecord] = []
        results: list[Any] = []
        for sample, frame in zip(samples, frames):
            result = process_frame(frame, background, config)
            contour_areas = [float(cv2.contourArea(c)) for c in result.contours]
            records.append(
                DetectionRecord(
                    sample_index=sample.sample_index,
                    condition=condition,
                    detected=not result.is_empty,
                    contour_count=len(result.contours),
                    mask_pixels=int(np.count_nonzero(result.mask)),
                    band_mask_pixels=int(np.count_nonzero(result.mask_band)),
                    max_contour_area=max(contour_areas, default=0.0),
                )
            )
            results.append(result)

        records_by_condition[condition] = records
        results_by_condition[condition] = results

    return frames_by_condition, records_by_condition, results_by_condition


def _clean_output_dir(output_dir: Path) -> None:
    for name in ("frames", "masks", "overlays"):
        shutil.rmtree(output_dir / name, ignore_errors=True)
    for name in (
        "metrics.json",
        "manifest.json",
        "sample_array_manifest.json",
        "README.md",
    ):
        path = output_dir / name
        if path.exists():
            path.unlink()


def _write_png(path: Path, image: Any) -> None:
    import cv2
    import numpy as np

    path.parent.mkdir(parents=True, exist_ok=True)
    arr = np.asarray(image)
    if arr.ndim == 3 and arr.shape[2] == 3:
        arr = cv2.cvtColor(arr, cv2.COLOR_RGB2BGR)
    if not cv2.imwrite(str(path), arr):
        raise RuntimeError(f"Failed to write image: {path}")


def _baseline_detected_sample_indices(
    records_by_condition: dict[str, list[DetectionRecord]],
) -> list[int]:
    return [
        record.sample_index
        for record in records_by_condition.get("baseline", [])
        if record.detected
    ]


def _representative_sample_index(
    samples: list[DatasetSample],
    records_by_condition: dict[str, list[DetectionRecord]],
) -> int:
    detected_indices = _baseline_detected_sample_indices(records_by_condition)
    if detected_indices:
        return detected_indices[0]
    return samples[0].sample_index


def _records_by_sample(
    records_by_condition: dict[str, list[DetectionRecord]],
) -> dict[int, dict[str, DetectionRecord]]:
    records_by_sample: dict[int, dict[str, DetectionRecord]] = {}
    for condition, records in records_by_condition.items():
        for record in records:
            records_by_sample.setdefault(record.sample_index, {})[condition] = record
    return records_by_sample


def _first_sample_matching(
    samples: list[DatasetSample],
    records_by_sample: dict[int, dict[str, DetectionRecord]],
    predicate: Callable[[dict[str, DetectionRecord]], bool],
) -> int | None:
    for sample in samples:
        if predicate(records_by_sample.get(sample.sample_index, {})):
            return sample.sample_index
    return None


def _representative_sample_cases(
    samples: list[DatasetSample],
    records_by_condition: dict[str, list[DetectionRecord]],
) -> list[dict[str, Any]]:
    """Pick stable sample cases that summarize empty, cell, and failure behavior."""
    records_by_sample = _records_by_sample(records_by_condition)
    cases: list[dict[str, Any]] = []
    seen_case_keys: set[str] = set()

    def add_case(
        key: str,
        title: str,
        sample_index: int | None,
        conditions: list[str],
        reason: str,
    ) -> None:
        if sample_index is None or key in seen_case_keys:
            return
        available = records_by_sample.get(sample_index, {})
        selected_conditions = [
            condition for condition in conditions if condition in available
        ]
        if not selected_conditions:
            return
        seen_case_keys.add(key)
        cases.append(
            {
                "key": key,
                "title": title,
                "sample_index": sample_index,
                "conditions": selected_conditions,
                "reason": reason,
                "metrics": {
                    condition: asdict(available[condition])
                    for condition in selected_conditions
                },
            }
        )

    empty_index = _first_sample_matching(
        samples,
        records_by_sample,
        lambda records: "baseline" in records and not records["baseline"].detected,
    )
    add_case(
        "empty_baseline_reference",
        "Empty/no-contour baseline reference",
        empty_index,
        ["baseline"],
        "First sampled HF frame where the baseline detector returned no filtered contours.",
    )

    detected_index = _first_sample_matching(
        samples,
        records_by_sample,
        lambda records: "baseline" in records and records["baseline"].detected,
    )
    add_case(
        "cell_positive_baseline",
        "Cell-positive baseline reference",
        detected_index,
        [
            "baseline",
            "brightness_high",
            "brightness_extreme_high",
            "contrast_high",
            "contrast_extreme_high",
        ],
        "First sampled HF frame where the baseline detector returned filtered contours.",
    )

    standard_low_failure_index = _first_sample_matching(
        samples,
        records_by_sample,
        lambda records: (
            "baseline" in records
            and records["baseline"].detected
            and (
                (
                    "brightness_low" in records
                    and not records["brightness_low"].detected
                )
                or ("contrast_low" in records and not records["contrast_low"].detected)
            )
        ),
    )
    add_case(
        "standard_low_condition_drop",
        "Standard low-condition detection drop",
        standard_low_failure_index,
        ["baseline", "brightness_low", "contrast_low"],
        "Cell-positive HF frame where a standard low brightness or contrast condition lost detection.",
    )

    extreme_low_failure_index = _first_sample_matching(
        samples,
        records_by_sample,
        lambda records: (
            "baseline" in records
            and records["baseline"].detected
            and (
                (
                    "brightness_extreme_low" in records
                    and not records["brightness_extreme_low"].detected
                )
                or (
                    "contrast_extreme_low" in records
                    and not records["contrast_extreme_low"].detected
                )
            )
        ),
    )
    add_case(
        "extreme_low_condition_drop",
        "Extreme low-condition detection drop",
        extreme_low_failure_index,
        ["baseline", "brightness_extreme_low", "contrast_extreme_low"],
        "Cell-positive HF frame where extreme low brightness and contrast conditions lose detection.",
    )

    fallback_conditions = ["baseline"]
    for sample in samples:
        if len(cases) >= 3:
            break
        add_case(
            f"fallback_sample_{sample.sample_index:05d}",
            "Fallback sampled frame",
            sample.sample_index,
            fallback_conditions,
            "Fallback case included to keep at least three reviewer sample cases when available.",
        )

    return cases


def _artifact_paths_for_case(case: dict[str, Any]) -> dict[str, Any]:
    sample_name = f"sample_{case['sample_index']:05d}"
    paths_by_condition = {}
    for condition in case["conditions"]:
        paths_by_condition[condition] = {
            "input": str(
                Path("frames") / condition / f"{sample_name}_input.png"
            ),
            "mask": str(Path("masks") / condition / f"{sample_name}_mask.png"),
            "overlay": str(
                Path("overlays") / condition / f"{sample_name}_overlay.png"
            ),
        }
    return {
        **case,
        "paths": paths_by_condition,
    }


def _read_png_rgb(path: Path) -> Any:
    import cv2

    image = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if image is None:
        raise RuntimeError(f"Failed to read image: {path}")
    if image.ndim == 2:
        return cv2.cvtColor(image, cv2.COLOR_GRAY2RGB)
    if image.shape[2] == 4:
        return cv2.cvtColor(image, cv2.COLOR_BGRA2RGB)
    return cv2.cvtColor(image, cv2.COLOR_BGR2RGB)


def _sample_case_contact_sheet(
    output_dir: Path,
    sample_case: dict[str, Any],
) -> Any:
    import cv2
    import numpy as np

    rows = []
    for condition in sample_case["conditions"]:
        paths = sample_case["paths"][condition]
        images = [
            _read_png_rgb(output_dir / paths["input"]),
            _read_png_rgb(output_dir / paths["mask"]),
            _read_png_rgb(output_dir / paths["overlay"]),
        ]
        row = np.concatenate(images, axis=1)
        label_bar = np.full((22, row.shape[1], 3), 32, dtype=np.uint8)
        metrics = sample_case["metrics"][condition]
        label = (
            f"{sample_case['key']} sample={sample_case['sample_index']} "
            f"{condition} detected={metrics['detected']} "
            f"contours={metrics['contour_count']} mask_pixels={metrics['mask_pixels']}"
        )
        cv2.putText(
            label_bar,
            label,
            (5, 15),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.42,
            (255, 255, 255),
            1,
        )
        rows.append(np.concatenate([label_bar, row], axis=0))

    separator = np.full((4, rows[0].shape[1], 3), 64, dtype=np.uint8)
    separated_rows = []
    for row in rows:
        if separated_rows:
            separated_rows.append(separator)
        separated_rows.append(row)
    return np.concatenate(separated_rows, axis=0)


def _overlay_image(image: Any, result: Any, config: Any, label: str) -> Any:
    import cv2
    import numpy as np

    _, _, ensure_grayscale, _ = _processing_module()
    gray = ensure_grayscale(image)
    h, w = gray.shape[:2]
    vis = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

    mask = result.mask > 0
    if np.any(mask):
        tint = np.zeros_like(vis)
        tint[:, :, 1] = 255
        vis[mask] = cv2.addWeighted(vis[mask], 0.55, tint[mask], 0.45, 0)

    band_margin = int(h * (1.0 - config.band_fraction) / 2.0)
    y_start = band_margin
    y_end = h - band_margin
    cv2.line(vis, (0, y_start), (w, y_start), (255, 255, 0), 1)
    cv2.line(vis, (0, y_end), (w, y_end), (255, 255, 0), 1)

    shifted_contours = []
    for contour in result.contours:
        shifted = contour.copy()
        shifted[:, :, 1] += y_start
        shifted_contours.append(shifted)
    cv2.drawContours(vis, shifted_contours, -1, (0, 0, 255), 1)

    text = f"{label} detected={not result.is_empty} contours={len(result.contours)}"
    cv2.putText(
        vis,
        text,
        (5, 16),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.42,
        (255, 255, 255),
        1,
    )
    return cv2.cvtColor(vis, cv2.COLOR_BGR2RGB)


def write_artifacts(
    output_dir: Path,
    samples: list[DatasetSample],
    frames_by_condition: dict[str, list[Any]],
    records_by_condition: dict[str, list[DetectionRecord]],
    results_by_condition: dict[str, list[Any]],
    dataset_info: dict[str, Any],
    processing_config: Any,
    background_mode: str,
    background_sample_count: int,
    command: str,
    clean: bool,
) -> dict[str, Path]:
    """Write review images, metrics JSON, manifest, and README."""
    output_dir.mkdir(parents=True, exist_ok=True)
    if clean:
        _clean_output_dir(output_dir)

    review_sample_index = _representative_sample_index(samples, records_by_condition)
    baseline_detected_indices = _baseline_detected_sample_indices(records_by_condition)
    sample_cases = [
        _artifact_paths_for_case(sample_case)
        for sample_case in _representative_sample_cases(samples, records_by_condition)
    ]

    artifact_paths: dict[str, Path] = {}
    for transform in TRANSFORMS:
        condition = transform.name
        for position, sample in enumerate(samples):
            sample_name = f"sample_{sample.sample_index:05d}"
            frame = frames_by_condition[condition][position]
            result = results_by_condition[condition][position]

            input_path = output_dir / "frames" / condition / f"{sample_name}_input.png"
            mask_path = output_dir / "masks" / condition / f"{sample_name}_mask.png"
            overlay_path = (
                output_dir / "overlays" / condition / f"{sample_name}_overlay.png"
            )

            _write_png(input_path, frame)
            _write_png(mask_path, result.mask)
            _write_png(
                overlay_path,
                _overlay_image(
                    frame,
                    result,
                    processing_config,
                    f"{condition}:{sample.sample_index}",
                ),
            )

            if sample.sample_index == review_sample_index:
                artifact_paths[f"{condition}_input"] = input_path
                artifact_paths[f"{condition}_mask"] = mask_path
                artifact_paths[f"{condition}_overlay"] = overlay_path

    for sample_case in sample_cases:
        contact_sheet_path = output_dir / f"sample_case_{sample_case['key']}.png"
        _write_png(
            contact_sheet_path,
            _sample_case_contact_sheet(output_dir, sample_case),
        )
        sample_case["contact_sheet"] = str(contact_sheet_path.relative_to(output_dir))
        artifact_paths[f"sample_case_{sample_case['key']}"] = contact_sheet_path

    per_frame = {
        condition: [asdict(record) for record in records]
        for condition, records in records_by_condition.items()
    }
    metrics = {
        "dataset": dataset_info,
        "processing_config": dict(vars(processing_config)),
        "background_mode": background_mode,
        "background_sample_count": background_sample_count,
        "baseline_detected_sample_indices": baseline_detected_indices,
        "review_sample_index": review_sample_index,
        "sample_cases": sample_cases,
        "review_sample_selection": (
            "First sampled frame where the baseline processing path returned "
            "at least one filtered contour; falls back to the first sampled frame."
        ),
        "detection_success_definition": (
            "A frame succeeds when the existing processing path returns at least "
            "one filtered contour in the middle band."
        ),
        "transforms": [asdict(transform) for transform in TRANSFORMS],
        "condition_summary": summarize_detection_records(records_by_condition),
        "per_frame": per_frame,
    }

    metrics_path = output_dir / "metrics.json"
    metrics_path.write_text(
        json.dumps(metrics, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    artifact_paths["metrics"] = metrics_path

    manifest = {
        "issue": "KIN-12",
        "command": command,
        "dataset": dataset_info,
        "background_mode": background_mode,
        "background_sample_count": background_sample_count,
        "baseline_detected_sample_indices": baseline_detected_indices,
        "review_sample_index": review_sample_index,
        "sample_cases": sample_cases,
        "transforms": [asdict(transform) for transform in TRANSFORMS],
        "review_paths": {
            **{
                key: str(path.relative_to(output_dir))
                for key, path in sorted(artifact_paths.items())
            },
            "manifest": "manifest.json",
            "readme": "README.md",
            "sample_array_manifest": "sample_array_manifest.json",
        },
    }
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    artifact_paths["manifest"] = manifest_path

    sample_array_manifest = {
        "issue": "KIN-12",
        "dataset": dataset_info,
        "selection": (
            "Representative sample cases are selected deterministically from the "
            "requested split slice: first empty baseline, first cell-positive "
            "baseline, first standard low-condition detection drop, and first "
            "extreme low-condition detection drop when available."
        ),
        "sample_cases": sample_cases,
    }
    sample_array_manifest_path = output_dir / "sample_array_manifest.json"
    sample_array_manifest_path.write_text(
        json.dumps(sample_array_manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    artifact_paths["sample_array_manifest"] = sample_array_manifest_path

    readme_path = output_dir / "README.md"
    readme_path.write_text(
        _readme_text(
            dataset_info,
            background_mode,
            background_sample_count,
            command,
            baseline_detected_indices,
            review_sample_index,
            sample_cases,
        ),
        encoding="utf-8",
    )
    artifact_paths["readme"] = readme_path
    return artifact_paths


def _readme_text(
    dataset_info: dict[str, Any],
    background_mode: str,
    background_sample_count: int,
    command: str,
    baseline_detected_indices: list[int],
    review_sample_index: int,
    sample_cases: list[dict[str, Any]],
) -> str:
    sample_case_lines = "\n".join(
        (
            f"- `{case['key']}`: sample `{case['sample_index']}`, "
            f"conditions `{case['conditions']}` - {case['reason']}"
        )
        for case in sample_cases
    )
    return f"""# KIN-12 Synthetic Condition Validation

This bundle validates the existing middle-band contour detection path on
`{dataset_info['name']}` with deterministic brightness and contrast variants,
including standard low/high cases and more extreme low/high cases.

## Dataset Slice

- Dataset: `{dataset_info['name']}`
- Config: `{dataset_info['config']}`
- Split expression: `{dataset_info['split_expression']}`
- Sample indices: `{dataset_info['sample_indices']}`
- Dataset revision: `{dataset_info['revision']}`
- Background mode: `{background_mode}`
- Background sample count: `{background_sample_count}`
- Baseline detected sample indices: `{baseline_detected_indices}`
- Reviewer-facing sample index: `{review_sample_index}`

## Representative Sample Cases

{sample_case_lines}

## Regenerate

Run from the repository root:

```bash
{command}
```

## Outputs

- `frames/` contains baseline and transformed input frames.
- `masks/` contains processed binary masks from the detection path.
- `overlays/` contains input frames with band boundaries, mask tint, and contours.
- `metrics.json` reports detection success/failure counts per condition and parity
  against baseline, including matching metrics for the representative sample cases.
- `manifest.json` records the exact dataset slice, transforms, and review paths.
- `sample_array_manifest.json` records the deterministic reviewer sample-case
  array with input, mask, overlay, and per-condition metric paths.
- `sample_case_*.png` contact sheets show each representative case as input,
  mask, and overlay columns for the selected conditions.

The sample-case array includes cell-positive frames from the Hugging Face
dataset when the requested slice contains baseline detections.
"""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Validate synthetic brightness/contrast detection behavior on a "
            "Hugging Face stream dataset."
        ),
    )
    parser.add_argument("--dataset", default=DATASET_NAME)
    parser.add_argument("--dataset-config", default=DATASET_CONFIG)
    parser.add_argument("--split", default=DATASET_SPLIT)
    parser.add_argument("--sample-start", type=int, default=DEFAULT_SAMPLE_START)
    parser.add_argument("--sample-count", type=int, default=DEFAULT_SAMPLE_COUNT)
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR)
    parser.add_argument(
        "--background-mode",
        choices=("transformed", "baseline"),
        default="transformed",
        help=(
            "Use condition-matched transformed backgrounds or the baseline "
            "background for every condition."
        ),
    )
    parser.add_argument(
        "--background-sample-count",
        type=int,
        default=DEFAULT_BACKGROUND_SAMPLE_COUNT,
        help="Number of leading sampled frames used to build each background.",
    )
    parser.add_argument("--blur", type=int, default=3)
    parser.add_argument("--threshold", type=int, default=8)
    parser.add_argument("--morph-kernel", type=int, default=3)
    parser.add_argument("--morph-iterations", type=int, default=1)
    parser.add_argument("--min-area", type=float, default=100.0)
    parser.add_argument("--band-fraction", type=float, default=0.5)
    parser.add_argument(
        "--no-clean",
        action="store_true",
        help="Do not remove prior generated images/JSON in the output directory.",
    )
    return parser.parse_args()


def _regeneration_command(args: argparse.Namespace) -> str:
    parts = [
        "PYTHONPATH=.python_deps",
        "HF_HOME=.cache/huggingface",
        "HF_DATASETS_CACHE=.cache/huggingface/datasets",
        "python",
        "scripts/synthetic_condition_validation.py",
        "--dataset",
        args.dataset,
        "--dataset-config",
        args.dataset_config,
        "--split",
        args.split,
        "--sample-start",
        str(args.sample_start),
        "--sample-count",
        str(args.sample_count),
        "--output-dir",
        args.output_dir,
        "--background-mode",
        args.background_mode,
        "--background-sample-count",
        str(args.background_sample_count),
        "--blur",
        str(args.blur),
        "--threshold",
        str(args.threshold),
        "--morph-kernel",
        str(args.morph_kernel),
        "--morph-iterations",
        str(args.morph_iterations),
        "--min-area",
        str(args.min_area),
        "--band-fraction",
        str(args.band_fraction),
    ]
    if args.no_clean:
        parts.append("--no-clean")
    return " ".join(shlex.quote(part) for part in parts)


def main() -> None:
    args = parse_args()
    ProcessingConfig, _, _, _ = _processing_module()
    processing_config = ProcessingConfig(
        gaussian_blur_size=args.blur,
        bg_subtract_threshold=args.threshold,
        morph_kernel_size=args.morph_kernel,
        morph_iterations=args.morph_iterations,
        min_contour_area=args.min_area,
        band_fraction=args.band_fraction,
    )

    samples, dataset_info = load_samples(
        args.dataset,
        args.dataset_config,
        args.split,
        args.sample_start,
        args.sample_count,
    )
    frames_by_condition, records_by_condition, results_by_condition = run_detection(
        samples,
        processing_config,
        args.background_mode,
        args.background_sample_count,
    )
    artifact_paths = write_artifacts(
        Path(args.output_dir),
        samples,
        frames_by_condition,
        records_by_condition,
        results_by_condition,
        dataset_info,
        processing_config,
        args.background_mode,
        args.background_sample_count,
        _regeneration_command(args),
        clean=not args.no_clean,
    )

    print(f"Dataset: {dataset_info['name']} {dataset_info['split_expression']}")
    print(f"Dataset revision: {dataset_info['revision']}")
    print(f"Review bundle: {Path(args.output_dir)}")
    print(f"Metrics: {artifact_paths['metrics']}")
    print("Condition summary:")
    for condition, summary in summarize_detection_records(records_by_condition).items():
        print(
            f"  {condition}: success={summary['detection_success_count']} "
            f"failure={summary['detection_failure_count']} "
            f"changed_from_baseline={summary['changed_from_baseline']}"
        )


if __name__ == "__main__":
    main()
