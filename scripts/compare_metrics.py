#!/usr/bin/env python3
"""
Compare two gold-standard JSON metric files.

Diffs two JSON metric files (e.g. Qt pipeline output vs gold standard) with
configurable per-field tolerances and prints a summary report: frame counts,
max/mean deltas per numeric field, and per-field failure counts.

Usage:
    python scripts/compare_metrics.py gold_standard.json qt_output.json
    python scripts/compare_metrics.py qt_output.json
        (gold path from GOLD_STANDARD_JSON env or scripts/gold_standard_dataset.json)
    python scripts/compare_metrics.py gold.json qt.json --tolerance deformability 0.001
    python scripts/compare_metrics.py gold.json qt.json -o report.txt
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# Gold-standard frame keys that are numeric (use tolerance)
NUMERIC_KEYS = [
    "deformability", "area", "area_um2", "area_ratio", "ring_ratio",
    "brightness_q1", "brightness_q2", "brightness_q3", "brightness_q4",
]

# Keys that must match exactly (boolean or integer)
EXACT_KEYS = [
    "frame_type", "index", "timestamp_ns",
    "is_valid", "touches_border", "has_single_inner_contour", "in_range", "inner_contour_count",
]

# Default absolute tolerance for numeric fields (conservative)
DEFAULT_NUMERIC_TOLERANCE = 1e-6

# Keys to compare (subset of frame that we diff)
COMPARE_KEYS = set(NUMERIC_KEYS + EXACT_KEYS)


def get_default_gold_path() -> Optional[Path]:
    """
    Resolve default gold-standard JSON path from GOLD_STANDARD_JSON env or
    scripts/gold_standard_dataset.json (reference_json_path). Paths in config
    are relative to repo root (parent of script dir).
    """
    env_path = os.environ.get("GOLD_STANDARD_JSON")
    if env_path:
        p = Path(env_path)
        if p.is_absolute() or p.exists():
            return p.resolve()
        script_dir = Path(__file__).resolve().parent
        repo_root = script_dir.parent
        return (repo_root / p).resolve() if (repo_root / p).exists() else p.resolve()
    config_path = Path(__file__).resolve().parent / "gold_standard_dataset.json"
    if not config_path.exists():
        return None
    try:
        with open(config_path, encoding="utf-8") as f:
            data = json.load(f)
        ref = data.get("reference_json_path")
        if not ref:
            return None
        script_dir = Path(__file__).resolve().parent
        repo_root = script_dir.parent
        return (repo_root / ref).resolve()
    except (json.JSONDecodeError, OSError):
        return None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare two gold-standard JSON metric files.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "gold",
        type=str,
        nargs="?",
        default=None,
        help="Path to gold-standard JSON (reference). If omitted, use GOLD_STANDARD_JSON or scripts/gold_standard_dataset.json.",
    )
    parser.add_argument(
        "candidate",
        type=str,
        nargs="?",
        default=None,
        help="Path to candidate JSON (e.g. Qt pipeline output). If only one positional given, it is the candidate.",
    )
    parser.add_argument(
        "--output", "-o",
        type=str,
        default=None,
        help="Write report to file instead of stdout",
    )
    parser.add_argument(
        "--tolerance", "-t",
        type=str,
        nargs=2,
        action="append",
        metavar=("FIELD", "VALUE"),
        default=[],
        help="Per-field absolute tolerance for numeric fields (e.g. -t deformability 0.001). Can be repeated.",
    )
    parser.add_argument(
        "--default-tolerance",
        type=float,
        default=DEFAULT_NUMERIC_TOLERANCE,
        help=f"Default absolute tolerance for numeric fields. Default: {DEFAULT_NUMERIC_TOLERANCE}",
    )
    parser.add_argument(
        "--match-by",
        type=str,
        choices=["index", "index_and_type"],
        default="index",
        help="Match frames by index only, or index and frame_type. Default: index",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def build_frame_index(frames: List[dict], match_by: str) -> Dict[Tuple[int, str], dict]:
    """Index frames by (index,) or (index, frame_type)."""
    out: Dict[Tuple[int, str], dict] = {}
    for fr in frames:
        idx = fr["index"]
        key = (idx, fr["frame_type"]) if match_by == "index_and_type" else (idx, "")
        if key in out:
            # Duplicate index: keep first (or we could error)
            continue
        out[key] = fr
    return out


def compare_frames(
    gold_frame: dict,
    cand_frame: dict,
    tolerances: Dict[str, float],
    default_tol: float,
) -> Tuple[bool, Dict[str, Any]]:
    """
    Compare one gold vs one candidate frame. Return (all_match, details).
    details: per-field status and numeric deltas where applicable.
    """
    details: Dict[str, Any] = {}
    all_ok = True

    for key in COMPARE_KEYS:
        if key not in gold_frame or key not in cand_frame:
            details[key] = {"match": False, "reason": "missing"}
            all_ok = False
            continue

        g, c = gold_frame[key], cand_frame[key]

        if key in EXACT_KEYS:
            if key == "timestamp_ns":
                # integer
                match = g == c
                details[key] = {"match": match, "gold": g, "candidate": c}
                if not match:
                    details[key]["delta"] = c - g
            else:
                match = g == c
                details[key] = {"match": match, "gold": g, "candidate": c}
            if not match:
                all_ok = False
            continue

        # Numeric
        try:
            gv, cv = float(g), float(c)
        except (TypeError, ValueError):
            details[key] = {"match": False, "reason": "not_numeric", "gold": g, "candidate": c}
            all_ok = False
            continue

        tol = tolerances.get(key, default_tol)
        delta = abs(cv - gv)
        match = delta <= tol
        details[key] = {
            "match": match,
            "gold": gv,
            "candidate": cv,
            "delta": delta,
            "tolerance": tol,
        }
        if not match:
            all_ok = False

    return all_ok, details


def run_comparison(
    gold_path: Path,
    cand_path: Path,
    tolerances: Dict[str, float],
    default_tol: float,
    match_by: str,
) -> Tuple[int, int, List[Tuple[dict, dict, bool, Dict[str, Any]]]]:
    """
    Load both files, match frames, compare. Return (matched_count, total_gold, list of (gold, cand, ok, details)).
    """
    gold_data = load_json(gold_path)
    cand_data = load_json(cand_path)

    gold_frames = gold_data.get("frames", [])
    cand_index = build_frame_index(cand_data.get("frames", []), match_by)

    results: List[Tuple[dict, dict, bool, Dict[str, Any]]] = []
    matched_count = 0

    for gf in gold_frames:
        idx = gf["index"]
        key = (idx, gf["frame_type"]) if match_by == "index_and_type" else (idx, "")
        cf = cand_index.get(key)
        if cf is None:
            results.append((gf, {}, False, {"_reason": "no_matching_candidate_frame"}))
            continue
        matched_count += 1
        ok, details = compare_frames(gf, cf, tolerances, default_tol)
        results.append((gf, cf, ok, details))

    return matched_count, len(gold_frames), results


def format_report(
    gold_path: Path,
    cand_path: Path,
    matched_count: int,
    total_gold: int,
    results: List[Tuple[dict, dict, bool, Dict[str, Any]]],
    tolerances: Dict[str, float],
    default_tol: float,
) -> str:
    """Produce a human-readable summary report."""
    lines: List[str] = []
    lines.append("Gold standard: " + str(gold_path))
    lines.append("Candidate:     " + str(cand_path))
    lines.append("")
    lines.append(f"Frames in gold: {total_gold}")
    lines.append(f"Frames matched: {matched_count}")
    missing = total_gold - matched_count
    if missing:
        lines.append(f"Frames missing in candidate: {missing}")
    lines.append("")

    failed = [r for r in results if not r[2]]
    lines.append(f"Frames with differences: {len(failed)}")
    if failed:
        lines.append("")

    # Per-field failure counts and numeric stats
    field_failures: Dict[str, int] = {k: 0 for k in COMPARE_KEYS}
    field_deltas: Dict[str, List[float]] = {k: [] for k in NUMERIC_KEYS}

    for _gf, _cf, ok, details in results:
        if "_reason" in details:
            continue
        for key, d in details.items():
            if not isinstance(d, dict) or d.get("match", True):
                continue
            if key in field_failures:
                field_failures[key] += 1
            if key in field_deltas and "delta" in d:
                field_deltas[key].append(d["delta"])

    lines.append("Per-field failure count:")
    for k in COMPARE_KEYS:
        n = field_failures.get(k, 0)
        if n > 0:
            lines.append(f"  {k}: {n}")
    lines.append("")

    lines.append("Numeric fields - max delta / mean delta (where differences exist):")
    for k in NUMERIC_KEYS:
        deltas = field_deltas.get(k, [])
        if not deltas:
            continue
        lines.append(f"  {k}: max={max(deltas):.6g}, mean={sum(deltas) / len(deltas):.6g} (tolerance={tolerances.get(k, default_tol):.6g})")
    lines.append("")

    if failed and len(failed) <= 20:
        lines.append("First few differing frames (index, gold vs candidate):")
        for gf, cf, _ok, details in failed[:10]:
            idx = gf.get("index", "?")
            diffs = [k for k, d in details.items() if isinstance(d, dict) and not d.get("match", True)]
            lines.append(f"  index {idx}: diffs in {diffs}")
    elif failed:
        lines.append("(More than 20 differing frames; omit per-frame list.)")

    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    # If only one positional given, it is the candidate; gold from config/env
    if args.candidate is None and args.gold is not None:
        cand_path = Path(args.gold)
        gold_path = get_default_gold_path()
        if gold_path is None:
            print("ERROR: Gold path required. Pass gold and candidate, or set GOLD_STANDARD_JSON or scripts/gold_standard_dataset.json.", file=sys.stderr)
            return 1
        gold_path = Path(gold_path)
    else:
        gold_path = Path(args.gold) if args.gold else get_default_gold_path()
        cand_path = Path(args.candidate) if args.candidate else None
        if gold_path is None:
            print("ERROR: Gold path required. Pass gold and candidate, or set GOLD_STANDARD_JSON or scripts/gold_standard_dataset.json.", file=sys.stderr)
            return 1
        gold_path = Path(gold_path)
        if cand_path is None:
            print("ERROR: Candidate path required.", file=sys.stderr)
            return 1

    if not gold_path.exists():
        print(f"ERROR: Gold file not found: {gold_path}", file=sys.stderr)
        return 1
    if not cand_path.exists():
        print(f"ERROR: Candidate file not found: {cand_path}", file=sys.stderr)
        return 1

    tolerances: Dict[str, float] = {}
    for field, val in args.tolerance:
        try:
            tolerances[field] = float(val)
        except ValueError:
            print(f"ERROR: Invalid tolerance value for {field}: {val}", file=sys.stderr)
            return 1

    matched_count, total_gold, results = run_comparison(
        gold_path,
        cand_path,
        tolerances,
        args.default_tolerance,
        args.match_by,
    )

    report = format_report(
        gold_path,
        cand_path,
        matched_count,
        total_gold,
        results,
        tolerances,
        args.default_tolerance,
    )

    if args.output:
        Path(args.output).write_text(report, encoding="utf-8")
        print(f"Report written to {args.output}")
    else:
        print(report)

    # Exit 0 only if all frames matched and no differences
    failed = sum(1 for r in results if not r[2])
    return 0 if matched_count == total_gold and failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
