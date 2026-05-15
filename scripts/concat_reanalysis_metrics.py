#!/usr/bin/env python3
"""
Concatenate reanalysis metrics CSV files and summarize object instances after
applying area and deformability gates.

Usage:
    python scripts/concat_reanalysis_metrics.py -i path/to/reanalysis -o path/to/summary

    python scripts/concat_reanalysis_metrics.py -i path/to/reanalysis -o path/to/summary \
        --area-min 250 --area-max 1200 --deform-min 0.0 --deform-max 1.0
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


DEFAULT_AREA_MIN = 250.0
DEFAULT_AREA_MAX = 1200.0
DEFAULT_DEFORM_MIN = 0.0
DEFAULT_DEFORM_MAX = 1.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Concatenate reanalysis metrics CSVs and summarize gated valid/invalid object instances.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--input", "-i", required=True, help="Directory containing metrics.csv files, or one metrics.csv file")
    parser.add_argument("--output", "-o", required=True, help="Output directory")
    parser.add_argument("--area-min", type=float, default=DEFAULT_AREA_MIN, help=f"Minimum valid area in pixels. Default: {DEFAULT_AREA_MIN}")
    parser.add_argument("--area-max", type=float, default=DEFAULT_AREA_MAX, help=f"Maximum valid area in pixels. Default: {DEFAULT_AREA_MAX}")
    parser.add_argument("--deform-min", type=float, default=DEFAULT_DEFORM_MIN, help=f"Minimum valid deformability. Default: {DEFAULT_DEFORM_MIN}")
    parser.add_argument("--deform-max", type=float, default=DEFAULT_DEFORM_MAX, help=f"Maximum valid deformability. Default: {DEFAULT_DEFORM_MAX}")
    parser.add_argument("--combined-name", default="combined_metrics.csv", help="Combined CSV filename. Default: combined_metrics.csv")
    parser.add_argument("--summary-name", default="summary.csv", help="Summary CSV filename. Default: summary.csv")
    return parser.parse_args()


def find_metrics_files(input_path: Path) -> list[Path]:
    if input_path.is_file():
        return [input_path]
    return sorted(input_path.rglob("metrics.csv"))


def parse_float(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, "") or 0.0)
    except ValueError:
        return 0.0


def parse_int(row: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(float(row.get(key, "") or default))
    except ValueError:
        return default


def source_label(metrics_path: Path, root: Path) -> str:
    if root.is_file():
        return metrics_path.stem
    try:
        rel_parent = metrics_path.parent.relative_to(root)
    except ValueError:
        rel_parent = metrics_path.parent
    return str(rel_parent)


def gate_row(
    row: dict[str, str],
    area_min: float,
    area_max: float,
    deform_min: float,
    deform_max: float,
) -> tuple[bool, list[str], bool]:
    object_id = parse_int(row, "Object Id", default=0)
    is_instance = object_id >= 0
    area = parse_float(row, "Area")
    deformability = parse_float(row, "Deformability")

    if not is_instance:
        return False, ["empty_frame"], False

    reasons: list[str] = []
    if area < area_min or area > area_max:
        reasons.append("area_out_of_range")
    if deformability < deform_min or deformability > deform_max:
        reasons.append("deformability_out_of_range")
    return len(reasons) == 0, reasons or ["valid"], True


def increment_bucket(bucket: dict[str, Any], gate_valid: bool, gate_reasons: list[str], is_instance: bool) -> None:
    bucket["rows"] += 1
    if not is_instance:
        bucket["empty_frames"] += 1
        return
    bucket["detected_objects"] += 1
    if gate_valid:
        bucket["valid_objects"] += 1
    else:
        bucket["invalid_objects"] += 1
        for reason in gate_reasons:
            bucket["invalid_reasons"][reason] = bucket["invalid_reasons"].get(reason, 0) + 1


def add_object_area(bucket: dict[str, Any], row: dict[str, str], is_instance: bool) -> None:
    """Accumulate area for every detected object row."""
    if not is_instance:
        return
    bucket["object_areas"].append(parse_float(row, "Area"))


def empty_summary_bucket() -> dict[str, Any]:
    return {
        "rows": 0,
        "empty_frames": 0,
        "detected_objects": 0,
        "valid_objects": 0,
        "invalid_objects": 0,
        "invalid_reasons": {},
        "object_areas": [],
    }


def invalid_reason_columns(invalid_reasons: set[str]) -> list[str]:
    return [f"Invalid: {reason}" for reason in sorted(invalid_reasons)]


def pct(numerator: float, denominator: float) -> float:
    return (numerator / denominator * 100.0) if denominator else 0.0


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * q
    lo = int(pos)
    hi = min(lo + 1, len(ordered) - 1)
    frac = pos - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def summary_row(source: str, data: dict[str, Any], invalid_reasons: set[str]) -> dict[str, Any]:
    detected_objects = data["detected_objects"]
    object_areas = data["object_areas"]
    area_count = len(object_areas)
    avg_object_area = sum(object_areas) / area_count if area_count else 0.0
    p10_object_area = percentile(object_areas, 0.10)
    p90_object_area = percentile(object_areas, 0.90)
    row: dict[str, Any] = {
        "Source": source,
        "Total Rows": data["rows"],
        "Empty Frames": data["empty_frames"],
        "Detected Objects": detected_objects,
        "Valid Objects": data["valid_objects"],
        "Valid % of Objects": f"{pct(data['valid_objects'], detected_objects):.2f}",
        "Invalid Objects": data["invalid_objects"],
        "Invalid % of Objects": f"{pct(data['invalid_objects'], detected_objects):.2f}",
        "Object Area Count": area_count,
        "Object Area Avg": f"{avg_object_area:.2f}",
        "Object Area P10": f"{p10_object_area:.2f}",
        "Object Area P90": f"{p90_object_area:.2f}",
        "Object Area P90-P10": f"{p90_object_area - p10_object_area:.2f}",
    }
    for reason in sorted(invalid_reasons):
        row[f"Invalid: {reason}"] = data["invalid_reasons"].get(reason, 0)
    return row


def write_summary_csv(path: Path, by_source: dict[str, dict[str, Any]], invalid_reasons: set[str]) -> None:
    fieldnames = [
        "Source",
        "Total Rows",
        "Empty Frames",
        "Detected Objects",
        "Valid Objects",
        "Valid % of Objects",
        "Invalid Objects",
        "Invalid % of Objects",
        "Object Area Count",
        "Object Area Avg",
        "Object Area P10",
        "Object Area P90",
        "Object Area P90-P10",
    ] + invalid_reason_columns(invalid_reasons)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for source, data in by_source.items():
            writer.writerow(summary_row(source, data, invalid_reasons))


def main() -> int:
    args = parse_args()
    input_path = Path(args.input)
    output_dir = Path(args.output)
    if not input_path.exists():
        print(f"ERROR: Input path does not exist: {input_path}", file=sys.stderr)
        return 1

    metrics_files = find_metrics_files(input_path)
    if not metrics_files:
        print(f"ERROR: No metrics.csv files found under {input_path}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)
    combined_path = output_dir / args.combined_name
    summary_path = output_dir / args.summary_name
    summary_json_path = output_dir / "summary.json"

    by_source: dict[str, dict[str, Any]] = defaultdict(empty_summary_bucket)
    total = empty_summary_bucket()
    combined_fieldnames: list[str] | None = None
    invalid_reasons: set[str] = set()

    with combined_path.open("w", newline="", encoding="utf-8") as combined_f:
        writer: csv.DictWriter | None = None
        for metrics_path in metrics_files:
            source = source_label(metrics_path, input_path)
            with metrics_path.open("r", newline="", encoding="utf-8") as input_f:
                reader = csv.DictReader(input_f)
                if reader.fieldnames is None:
                    continue
                extra_fields = ["Source", "Source CSV", "Gate Valid", "Gate Reason", "Gate Area Min", "Gate Area Max", "Gate Deform Min", "Gate Deform Max"]
                if combined_fieldnames is None:
                    combined_fieldnames = extra_fields + list(reader.fieldnames)
                    writer = csv.DictWriter(combined_f, fieldnames=combined_fieldnames)
                    writer.writeheader()
                elif list(reader.fieldnames) != combined_fieldnames[len(extra_fields):]:
                    print(f"ERROR: CSV header mismatch: {metrics_path}", file=sys.stderr)
                    return 1

                for row in reader:
                    gate_valid, gate_reasons, is_instance = gate_row(
                        row,
                        args.area_min,
                        args.area_max,
                        args.deform_min,
                        args.deform_max,
                    )
                    if is_instance and not gate_valid:
                        invalid_reasons.update(gate_reasons)
                    increment_bucket(by_source[source], gate_valid, gate_reasons, is_instance)
                    increment_bucket(total, gate_valid, gate_reasons, is_instance)
                    add_object_area(by_source[source], row, is_instance)
                    add_object_area(total, row, is_instance)
                    out_row = dict(row)
                    out_row.update({
                        "Source": source,
                        "Source CSV": str(metrics_path),
                        "Gate Valid": "Yes" if gate_valid else "No",
                        "Gate Reason": ";".join(gate_reasons),
                        "Gate Area Min": f"{args.area_min:g}",
                        "Gate Area Max": f"{args.area_max:g}",
                        "Gate Deform Min": f"{args.deform_min:g}",
                        "Gate Deform Max": f"{args.deform_max:g}",
                    })
                    assert writer is not None
                    writer.writerow(out_row)

    ordered_summary: dict[str, dict[str, Any]] = {"TOTAL": total}
    ordered_summary.update(dict(sorted(by_source.items())))
    write_summary_csv(summary_path, ordered_summary, invalid_reasons)
    summary_payload = {
        "input": str(input_path),
        "metrics_files": len(metrics_files),
        "area_min": args.area_min,
        "area_max": args.area_max,
        "deform_min": args.deform_min,
        "deform_max": args.deform_max,
        "summary": ordered_summary,
    }
    summary_json_path.write_text(json.dumps(summary_payload, indent=2), encoding="utf-8")

    print(f"Read {len(metrics_files)} metrics.csv files")
    print(f"Wrote combined CSV: {combined_path}")
    print(f"Wrote summary CSV: {summary_path}")
    print(f"Wrote summary JSON: {summary_json_path}")
    print(
        "TOTAL: "
        f"rows={total['rows']}, "
        f"empty_frames={total['empty_frames']}, "
        f"detected_objects={total['detected_objects']}, "
        f"valid={total['valid_objects']}, "
        f"invalid={total['invalid_objects']}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
