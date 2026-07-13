#!/usr/bin/env python3
"""
Convert a legacy MIB-Studio metrics CSV export to gold-standard JSON.

The legacy (pre-Qt) MIB-Studio application exports per-cell metrics as CSV.
That CSV historically carries fewer fields than the gold-standard schema
(docs/gold_standard_metrics.schema.json) -- notably it has no per-frame
area_ratio or brightness-quantile columns. This converter maps the columns
documented in docs/gold_standard_metrics.md ("Batch, Condition, ImageIndex,
Timestamp_us, Deformability, Area, RingRatio, Valid, Method, ProcessingConfig")
onto the gold-standard schema, and fills unavailable fields with documented
defaults (printed as warnings so the gap is visible, not silent).

Column names are configurable via CLI flags for real legacy exports whose
headers differ from the documented example. `Batch`/`Condition` are folded
into the document's top-level "source" label (schema has no room for
per-frame passthrough fields); `Method`/`ProcessingConfig` are informational
only in the legacy export and are not part of the gold-standard contract, so
they are reported (not silently dropped) but not written into the output.

Usage:
    python scripts/convert_legacy_csv_to_json.py \\
        -i path/to/metrics_output.csv -o path/to/gold_standard.json \\
        -p 0.4886 --source MIB-Studio-gold

    # Real legacy export with different column names
    python scripts/convert_legacy_csv_to_json.py -i legacy.csv -o gold.json \\
        --index-column FrameIndex --area-column AreaPx --valid-column IsValid
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

GOLD_STANDARD_SCHEMA_VERSION = 1

# Fields the documented legacy CSV format does not carry. Converted rows use
# these defaults; a one-time warning is printed per missing column so the gap
# stays visible to whoever runs the conversion.
DEFAULT_OBJECT_ID = -1
DEFAULT_OBJECT_COUNT = 1
DEFAULT_AREA_RATIO = 0.0
DEFAULT_TOUCHES_BORDER = False
DEFAULT_BRIGHTNESS = 0.0

TRUE_VALUES = {"1", "true", "yes", "y", "valid"}
FALSE_VALUES = {"0", "false", "no", "n", "invalid"}


def parse_bool(value: str, *, field: str) -> bool:
    normalized = value.strip().lower()
    if normalized in TRUE_VALUES:
        return True
    if normalized in FALSE_VALUES:
        return False
    raise ValueError(f"Cannot parse boolean value {value!r} for field {field!r}")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", "-i", required=True, help="Path to legacy MIB-Studio metrics CSV")
    parser.add_argument("--output", "-o", required=True, help="Path to write gold-standard JSON")
    parser.add_argument("--pixel-to-micron", "-p", type=float, default=0.4886, help="Pixel to micron conversion factor. Default: 0.4886")
    parser.add_argument("--source", default=None, help="Value for the document's 'source' field. Defaults to 'MIB-Studio-gold', or 'MIB-Studio-gold:<batch>:<condition>' if those columns are present and consistent")
    parser.add_argument("--index-column", default="ImageIndex")
    parser.add_argument("--timestamp-us-column", default="Timestamp_us")
    parser.add_argument("--deformability-column", default="Deformability")
    parser.add_argument("--area-column", default="Area")
    parser.add_argument("--ring-ratio-column", default="RingRatio")
    parser.add_argument("--valid-column", default="Valid")
    parser.add_argument("--batch-column", default="Batch")
    parser.add_argument("--condition-column", default="Condition")
    parser.add_argument("--method-column", default="Method")
    parser.add_argument("--config-column", default="ProcessingConfig")
    return parser


def _require_column(fieldnames: List[str], column: str, role: str) -> None:
    if column not in fieldnames:
        raise ValueError(f"Required column {column!r} ({role}) not found in CSV header: {fieldnames}")


def convert_rows(
    rows: List[Dict[str, str]],
    fieldnames: List[str],
    *,
    pixel_to_micron: float,
    index_column: str,
    timestamp_us_column: str,
    deformability_column: str,
    area_column: str,
    ring_ratio_column: str,
    valid_column: str,
    batch_column: str,
    condition_column: str,
) -> tuple[List[Dict[str, Any]], List[str]]:
    """Convert legacy rows to gold-standard frame dicts. Returns (frames, warnings)."""
    warnings: List[str] = []

    for required, role in (
        (index_column, "frame index"),
        (deformability_column, "deformability"),
        (area_column, "area"),
        (valid_column, "validity"),
    ):
        _require_column(fieldnames, required, role)

    has_timestamp = timestamp_us_column in fieldnames
    has_ring_ratio = ring_ratio_column in fieldnames
    if not has_timestamp:
        warnings.append(f"column {timestamp_us_column!r} not found; timestamp_ns defaulted to 0")
    if not has_ring_ratio:
        warnings.append(f"column {ring_ratio_column!r} not found; ring_ratio defaulted to 0.0")
    warnings.append(
        "legacy CSV has no area_ratio or brightness-quantile columns; "
        f"area_ratio defaulted to {DEFAULT_AREA_RATIO}, brightness_q1..q4 defaulted to {DEFAULT_BRIGHTNESS}, "
        f"touches_border defaulted to {DEFAULT_TOUCHES_BORDER}, object_id defaulted to {DEFAULT_OBJECT_ID}, "
        f"object_count defaulted to {DEFAULT_OBJECT_COUNT}"
    )

    frames: List[Dict[str, Any]] = []
    for row_num, row in enumerate(rows, start=2):  # header is line 1
        try:
            is_valid = parse_bool(row[valid_column], field=valid_column)
            area = float(row[area_column])
            index = int(float(row[index_column]))
            deformability = float(row[deformability_column])
            timestamp_ns = int(float(row[timestamp_us_column]) * 1000) if has_timestamp else 0
            ring_ratio = float(row[ring_ratio_column]) if has_ring_ratio else 0.0
        except (ValueError, KeyError) as exc:
            raise ValueError(f"Row {row_num}: {exc}") from exc

        frames.append({
            "frame_type": "valid" if is_valid else "invalid",
            "index": index,
            "timestamp_ns": timestamp_ns,
            "object_id": DEFAULT_OBJECT_ID,
            "object_count": DEFAULT_OBJECT_COUNT,
            "deformability": deformability,
            "area": area,
            "area_um2": area * pixel_to_micron * pixel_to_micron,
            "area_ratio": DEFAULT_AREA_RATIO,
            "ring_ratio": ring_ratio,
            "is_valid": is_valid,
            "touches_border": DEFAULT_TOUCHES_BORDER,
            "has_single_inner_contour": is_valid,
            "in_range": is_valid,
            "inner_contour_count": 1 if is_valid else 0,
            "brightness_q1": DEFAULT_BRIGHTNESS,
            "brightness_q2": DEFAULT_BRIGHTNESS,
            "brightness_q3": DEFAULT_BRIGHTNESS,
            "brightness_q4": DEFAULT_BRIGHTNESS,
        })

    return frames, warnings


def resolve_source_label(
    explicit_source: Optional[str],
    rows: List[Dict[str, str]],
    batch_column: str,
    condition_column: str,
) -> str:
    if explicit_source:
        return explicit_source
    if rows and batch_column in rows[0] and condition_column in rows[0]:
        batches = {row.get(batch_column, "") for row in rows}
        conditions = {row.get(condition_column, "") for row in rows}
        if len(batches) == 1 and len(conditions) == 1:
            batch = next(iter(batches))
            condition = next(iter(conditions))
            if batch or condition:
                return f"MIB-Studio-gold:{batch}:{condition}"
    return "MIB-Studio-gold"


def main(argv: Optional[List[str]] = None) -> int:
    args = build_arg_parser().parse_args(argv)
    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.is_file():
        print(f"ERROR: input CSV does not exist: {input_path}", file=sys.stderr)
        return 1

    with input_path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        fieldnames = reader.fieldnames or []
        rows = list(reader)

    extra_columns = set(fieldnames) - {
        args.index_column, args.timestamp_us_column, args.deformability_column,
        args.area_column, args.ring_ratio_column, args.valid_column,
        args.batch_column, args.condition_column, args.method_column, args.config_column,
    }
    if extra_columns:
        print(f"NOTE: ignoring unrecognized columns not part of the gold-standard contract: {sorted(extra_columns)}", file=sys.stderr)
    if args.method_column in fieldnames or args.config_column in fieldnames:
        print(
            f"NOTE: {args.method_column!r}/{args.config_column!r} are informational in the legacy CSV "
            "and are not part of the gold-standard per-frame schema; not written to output.",
            file=sys.stderr,
        )

    try:
        frames, warnings = convert_rows(
            rows,
            fieldnames,
            pixel_to_micron=args.pixel_to_micron,
            index_column=args.index_column,
            timestamp_us_column=args.timestamp_us_column,
            deformability_column=args.deformability_column,
            area_column=args.area_column,
            ring_ratio_column=args.ring_ratio_column,
            valid_column=args.valid_column,
            batch_column=args.batch_column,
            condition_column=args.condition_column,
        )
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    for warning in warnings:
        print(f"WARNING: {warning}", file=sys.stderr)

    document = {
        "version": GOLD_STANDARD_SCHEMA_VERSION,
        "pixel_to_micron": args.pixel_to_micron,
        "source": resolve_source_label(args.source, rows, args.batch_column, args.condition_column),
        "frames": frames,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"Converted {len(frames)} rows -> {output_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
