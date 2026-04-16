"""Upload thread_perf_test results to MLflow.

Reads a JSON file produced by `thread_perf_test` and logs each metric
(min/median/mean/p95/p99/max and the sample count) under a single MLflow
run. Intended to be invoked from CI after `ctest -R thread_perf_test`
completes, but works just as well locally.

Auth: set MLFLOW_TRACKING_USERNAME + MLFLOW_TRACKING_PASSWORD in the
environment (per the repo convention in CLAUDE.md) — this script refuses
to run without them when talking to an HTTPS tracking server.

Tracking URI defaults to https://mlflow.yofo.bio (repo convention);
override with --tracking-uri or MLFLOW_TRACKING_URI.

Usage:
    python scripts/upload_thread_perf.py \
        --json build/thread_perf_results.json \
        --experiment thread-performance \
        --run-name "$(git rev-parse --short HEAD)"
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict


def load_report(path: Path) -> Dict[str, Dict[str, float]]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"Expected top-level JSON object in {path}, got {type(data).__name__}")
    return data


def flatten_metrics(report: Dict[str, Dict[str, float]]) -> Dict[str, float]:
    """Flatten {bench: {metric: value}} to {bench.metric: value}.

    Example: {"trigger_wakeup_idle": {"p99_us": 12.3}} →
             {"trigger_wakeup_idle.p99_us": 12.3}.
    """
    out: Dict[str, float] = {}
    for bench, stats in report.items():
        if not isinstance(stats, dict):
            continue
        for metric, value in stats.items():
            if not isinstance(value, (int, float)):
                continue
            out[f"{bench}.{metric}"] = float(value)
    return out


def parse_args(argv: list[str]) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", required=True, type=Path,
                    help="Path to thread_perf_results.json")
    ap.add_argument("--tracking-uri",
                    default=os.environ.get("MLFLOW_TRACKING_URI", "https://mlflow.yofo.bio"),
                    help="MLflow tracking URI")
    ap.add_argument("--experiment", default="thread-performance",
                    help="MLflow experiment name")
    ap.add_argument("--run-name", default=None,
                    help="MLflow run name (defaults to git SHA or 'local')")
    ap.add_argument("--tag", action="append", default=[],
                    metavar="KEY=VALUE",
                    help="Additional tag to attach to the run (repeatable)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Parse and print metrics but don't upload")
    return ap.parse_args(argv)


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


def parse_tags(items: list[str]) -> Dict[str, str]:
    tags: Dict[str, str] = {}
    for item in items:
        if "=" not in item:
            raise ValueError(f"--tag expects KEY=VALUE, got {item!r}")
        k, v = item.split("=", 1)
        tags[k.strip()] = v.strip()
    return tags


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    report = load_report(args.json)
    metrics = flatten_metrics(report)

    if not metrics:
        print(f"No numeric metrics found in {args.json}", file=sys.stderr)
        return 1

    if args.dry_run:
        print(f"[dry-run] tracking_uri={args.tracking_uri}")
        print(f"[dry-run] experiment={args.experiment}")
        print(f"[dry-run] run_name={resolve_run_name(args.run_name)}")
        for k, v in sorted(metrics.items()):
            print(f"[dry-run] metric {k} = {v}")
        return 0

    # Credentials gate — per CLAUDE.md "do not hardcode credentials".
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
        mlflow.log_artifact(str(args.json))
        print(f"Logged {len(metrics)} metrics to run {run.info.run_id} "
              f"(experiment {args.experiment!r}, name {run_name!r})")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
