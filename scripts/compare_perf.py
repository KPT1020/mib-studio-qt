"""Compare two perf suite JSON reports and flag regressions.

Takes a baseline and a current perf_summary.json (or individual test
JSON), flattens both, and reports per-metric deltas. Flags regressions
that exceed a configurable threshold (default 10%).

Usage:
    python scripts/compare_perf.py \\
        --baseline build.baseline/perf_summary.json \\
        --current  build/perf_summary.json

    python scripts/compare_perf.py \\
        --baseline build.baseline/thread_perf_results.json \\
        --current  build/thread_perf_results.json \\
        --threshold 0.05

Exit code 0 if no regressions exceed the threshold, 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Tuple


def flatten(prefix: str, value: Any, out: Dict[str, float]) -> None:
    if isinstance(value, dict):
        for k, v in value.items():
            key = f"{prefix}.{k}" if prefix else str(k)
            flatten(key, v, out)
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        out[prefix] = float(value)


def load_and_flatten(path: Path) -> Dict[str, float]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    out: Dict[str, float] = {}
    flatten("", data, out)
    return out


HIGHER_IS_BETTER = {"frames_per_s", "mb_per_s", "implied_fps",
                     "wall_clock_fps", "frames_processed",
                     "jobs_processed", "jobs_queued",
                     "pushed_count", "gotten_count"}


def is_higher_better(key: str) -> bool:
    return any(key.endswith(suffix) for suffix in HIGHER_IS_BETTER)


def compare(
    baseline: Dict[str, float],
    current: Dict[str, float],
    threshold: float,
) -> Tuple[List[dict], List[dict], List[dict]]:
    regressions = []
    improvements = []
    unchanged = []

    all_keys = sorted(set(baseline) | set(current))
    for key in all_keys:
        if key not in baseline or key not in current:
            continue
        old = baseline[key]
        new = current[key]
        if old == 0.0:
            continue
        delta_pct = (new - old) / abs(old) * 100.0
        higher_better = is_higher_better(key)

        entry = {
            "key": key,
            "baseline": old,
            "current": new,
            "delta_pct": delta_pct,
            "higher_better": higher_better,
        }

        is_regression = (
            (not higher_better and delta_pct > threshold * 100)
            or (higher_better and delta_pct < -threshold * 100)
        )
        is_improvement = (
            (not higher_better and delta_pct < -threshold * 100)
            or (higher_better and delta_pct > threshold * 100)
        )

        if is_regression:
            regressions.append(entry)
        elif is_improvement:
            improvements.append(entry)
        else:
            unchanged.append(entry)

    return regressions, improvements, unchanged


def fmt(v: float) -> str:
    if abs(v) >= 1000:
        return f"{v:,.0f}"
    if abs(v) >= 1:
        return f"{v:.2f}"
    return f"{v:.4f}"


def print_table(label: str, entries: List[dict]) -> None:
    if not entries:
        return
    print(f"\n{label} ({len(entries)}):")
    print(f"  {'Metric':<55} {'Baseline':>12} {'Current':>12} {'Delta':>9}")
    print(f"  {'-'*55} {'-'*12} {'-'*12} {'-'*9}")
    for e in entries:
        sign = "+" if e["delta_pct"] >= 0 else ""
        print(f"  {e['key']:<55} {fmt(e['baseline']):>12} "
              f"{fmt(e['current']):>12} {sign}{e['delta_pct']:>7.1f}%")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--baseline", required=True, type=Path)
    ap.add_argument("--current", required=True, type=Path)
    ap.add_argument("--threshold", type=float, default=0.10,
                    help="Regression threshold as a fraction (default: 0.10 = 10%%)")
    ap.add_argument("--json-out", type=Path, default=None,
                    help="Write comparison results as JSON")
    args = ap.parse_args(argv)

    bl = load_and_flatten(args.baseline)
    cu = load_and_flatten(args.current)

    regressions, improvements, unchanged = compare(bl, cu, args.threshold)

    print(f"Comparing {args.current} vs baseline {args.baseline} "
          f"(threshold: {args.threshold*100:.0f}%)")
    print(f"Metrics compared: {len(bl & cu.keys())}")

    print_table("REGRESSIONS", regressions)
    print_table("IMPROVEMENTS", improvements)

    if not regressions:
        print(f"\nNo regressions above {args.threshold*100:.0f}% threshold.")

    if args.json_out:
        result = {
            "threshold": args.threshold,
            "regressions": regressions,
            "improvements": improvements,
            "unchanged_count": len(unchanged),
        }
        args.json_out.write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(f"Wrote comparison to {args.json_out}")

    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
