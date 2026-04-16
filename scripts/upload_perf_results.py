"""Upload MIB-Studio performance test JSON reports to MLflow.

Reads one or more JSON reports produced by the perf test binaries
(thread_perf_test, framestore_perf_test, processing_perf_test,
hdf5_perf_test, capture_processing_test) and logs their metrics to
MLflow under a single run.

Metric namespacing:

    Each JSON file contributes metrics under a namespace derived from
    its filename stem (with `_results` stripped). E.g.
    `thread_perf_results.json` -> `thread_perf.*`. The metric path
    inside the file is then flattened with dots:

        thread_perf_results.json
          { "trigger_wakeup_idle": { "p99_us": 42.0 } }

    becomes:

        mlflow metric "thread_perf.trigger_wakeup_idle.p99_us" = 42.0

Auth: per CLAUDE.md conventions, credentials come from
MLFLOW_TRACKING_USERNAME + MLFLOW_TRACKING_PASSWORD -- this script
refuses to upload to HTTPS tracking servers without them.

Tracking URI defaults to https://mlflow.yofo.bio; override with
--tracking-uri or MLFLOW_TRACKING_URI. Experiment defaults to
`mib-studio-perf`.

Usage:
    python scripts/upload_perf_results.py \\
        --json build/thread_perf_results.json \\
        --json build/framestore_perf_results.json \\
        --json build/processing_perf_results.json \\
        --json build/hdf5_perf_results.json \\
        --json build/capture_processing_results.json \\
        --tag ci=github --tag build=release
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Dict, List


def load_report(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"Expected top-level JSON object in {path}, got {type(data).__name__}")
    return data


def namespace_from_path(path: Path) -> str:
    stem = path.stem
    # Strip trailing "_results" / "_report" so thread_perf_results -> thread_perf.
    stem = re.sub(r"_(?:results|report)$", "", stem)
    # Sanitise to something MLflow accepts in metric keys.
    return re.sub(r"[^A-Za-z0-9_.-]", "_", stem)


def flatten(prefix: str, value: Any, out: Dict[str, float]) -> None:
    """Recursively flatten nested dicts into {dotted.key: number}."""
    if isinstance(value, dict):
        for k, v in value.items():
            key = f"{prefix}.{k}" if prefix else str(k)
            flatten(key, v, out)
    elif isinstance(value, (int, float)) and not isinstance(value, bool):
        out[prefix] = float(value)
    # Non-numeric leaves (strings, bools, lists) are skipped -- MLflow
    # only takes numeric metrics. Strings go to tags via --tag.


def collect_metrics(paths: List[Path]) -> Dict[str, float]:
    all_metrics: Dict[str, float] = {}
    for p in paths:
        try:
            report = load_report(p)
        except Exception as ex:
            print(f"Skipping {p}: {ex}", file=sys.stderr)
            continue
        ns = namespace_from_path(p)
        flattened: Dict[str, float] = {}
        flatten(ns, report, flattened)
        collisions = set(all_metrics) & set(flattened)
        if collisions:
            print(f"Warning: metric key collisions when loading {p}: {sorted(collisions)}",
                  file=sys.stderr)
        all_metrics.update(flattened)
    return all_metrics


def parse_tags(items: List[str]) -> Dict[str, str]:
    tags: Dict[str, str] = {}
    for item in items:
        if "=" not in item:
            raise ValueError(f"--tag expects KEY=VALUE, got {item!r}")
        k, v = item.split("=", 1)
        tags[k.strip()] = v.strip()
    return tags


def resolve_run_name(explicit: str | None) -> str:
    if explicit:
        return explicit
    env = os.environ.get("GITHUB_SHA") or os.environ.get("BUILD_SOURCEVERSION")
    if env:
        return env[:12]
    try:
        import subprocess
        sha = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
        if sha:
            return sha
    except Exception:
        pass
    return "local"


def parse_args(argv: List[str]) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="append", required=True, type=Path,
                    metavar="PATH",
                    help="Path to a perf JSON report. Repeat for multiple files.")
    ap.add_argument("--tracking-uri",
                    default=os.environ.get("MLFLOW_TRACKING_URI", "https://mlflow.yofo.bio"),
                    help="MLflow tracking URI (default: https://mlflow.yofo.bio)")
    ap.add_argument("--experiment", default="mib-studio-perf",
                    help="MLflow experiment name (default: mib-studio-perf)")
    ap.add_argument("--run-name", default=None,
                    help="MLflow run name (defaults to short git SHA or 'local')")
    ap.add_argument("--tag", action="append", default=[], metavar="KEY=VALUE",
                    help="Additional tag to attach to the run (repeatable)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Parse and print metrics but don't upload")
    return ap.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)

    metrics = collect_metrics(args.json)
    if not metrics:
        print(f"No numeric metrics found in {[str(p) for p in args.json]}",
              file=sys.stderr)
        return 1

    if args.dry_run:
        print(f"[dry-run] tracking_uri={args.tracking_uri}")
        print(f"[dry-run] experiment={args.experiment}")
        print(f"[dry-run] run_name={resolve_run_name(args.run_name)}")
        for k, v in sorted(metrics.items()):
            print(f"[dry-run] metric {k} = {v}")
        return 0

    if args.tracking_uri.startswith("https://"):
        missing = [v for v in ("MLFLOW_TRACKING_USERNAME", "MLFLOW_TRACKING_PASSWORD")
                   if not os.environ.get(v)]
        if missing:
            print(f"Refusing to upload: missing env vars: {', '.join(missing)}",
                  file=sys.stderr)
            return 2

    try:
        import mlflow  # type: ignore
    except ImportError:
        print("mlflow not installed. Install with: pip install mlflow", file=sys.stderr)
        return 3

    mlflow.set_tracking_uri(args.tracking_uri)
    mlflow.set_experiment(args.experiment)

    run_name = resolve_run_name(args.run_name)
    tags = parse_tags(args.tag)

    with mlflow.start_run(run_name=run_name) as run:
        for k, v in tags.items():
            mlflow.set_tag(k, v)
        for metric_key, metric_val in metrics.items():
            mlflow.log_metric(metric_key, metric_val)
        for p in args.json:
            if p.exists():
                mlflow.log_artifact(str(p))
        print(f"Logged {len(metrics)} metrics to run {run.info.run_id} "
              f"(experiment {args.experiment!r}, name {run_name!r})")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
