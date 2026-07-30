#!/usr/bin/env python3
"""Upload a pipeline-timing dump so it is durably saved and reviewable by AI
agents (or anyone) away from the site machine.

Usage:
    python3 scripts/upload_pipeline_diagnostics.py <dump_dir> [--run-name NAME]
        [--tag key=value ...] [--offline]

<dump_dir> is the directory the app / mock harness wrote:
pipeline_trend.csv (MIB_PIPELINE_TREND=1) plus pipeline_frames.csv /
pipeline_triggers.csv / pipeline_skips.csv (MIB_PIPELINE_TIMING=1). See
docs/howto/pipeline-latency-diagnosis.md.

Primary sink: MLflow (repo convention — AGENTS.md sends test performance
metrics to mlflow.yofo.bio). The run gets:
  - params: host, platform, realtime mode / drop-frames / experiment flags
    (read from the trend CSV), session duration, operator tags
  - metrics: per-minute median-of-p95 series (step = minute) for the key
    latency/depth/CPU columns, plus final steady-state ratios
  - artifacts: every CSV in the dump dir + the analyzer's full report
    (analysis.txt), so a reviewer re-runs nothing

Credentials come from the environment, never hardcoded:
    MLFLOW_TRACKING_URI (default https://mlflow.yofo.bio)
    MLFLOW_TRACKING_USERNAME / MLFLOW_TRACKING_PASSWORD

Fallback (--offline, mlflow not installed, or server unreachable): the dump
dir is zipped next to itself; attach the zip to a GitHub issue on the repo
and an AI agent session can fetch and analyze it from there.
"""
from __future__ import annotations

import argparse
import os
import platform
import socket
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_pipeline_timing as apt  # noqa: E402

# Metric columns worth a step series in MLflow; ratios are logged for the
# analyzer's full gauge list.
SERIES_COLUMNS = [
    "e2e_frame_p95_us", "e2e_target_p95_us", "frame_age_p95_us", "algo_p95_us",
    "request_to_fire_p95_us", "backlog_frames", "batch_queue_depth",
    "cpu_realtime_pct", "cpu_capture_pct", "cpu_hdf_writer_pct",
    "hdf_write_avg_us", "overlay_avg_us", "heap_inuse_mb", "heap_free_mb",
    "mem_mb",
]


def write_analysis_report(dump_dir: Path) -> Path:
    """Run the analyzer and persist its full report next to the CSVs."""
    result = subprocess.run(
        [sys.executable, str(Path(__file__).resolve().parent / "analyze_pipeline_timing.py"),
         str(dump_dir)],
        capture_output=True, text=True)
    report = dump_dir / "analysis.txt"
    report.write_text(result.stdout + (("\n" + result.stderr) if result.stderr else ""))
    return report


def zip_fallback(dump_dir: Path) -> Path:
    zip_path = dump_dir.with_name(dump_dir.name + "_diagnostics.zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(dump_dir.iterdir()):
            if f.is_file():
                z.write(f, f.name)
    return zip_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump_dir", type=Path)
    parser.add_argument("--run-name", default=None,
                        help="MLflow run name (default: <host>-<dump dir name>)")
    parser.add_argument("--experiment-name", default="pipeline-latency-diagnostics")
    parser.add_argument("--tag", action="append", default=[], metavar="KEY=VALUE",
                        help="extra run tag(s), e.g. --tag site=lab1 --tag ticket=123")
    parser.add_argument("--offline", action="store_true",
                        help="skip MLflow, just produce the zip for issue attachment")
    args = parser.parse_args()

    dump_dir = args.dump_dir.resolve()
    trend_path = dump_dir / "pipeline_trend.csv"
    if not trend_path.exists():
        print(f"error: {trend_path} not found — run with MIB_PIPELINE_TREND=1", file=sys.stderr)
        return 2
    rows = apt.read_trend_csv(trend_path)
    if not rows:
        print(f"error: {trend_path} is empty", file=sys.stderr)
        return 2

    report = write_analysis_report(dump_dir)
    print(f"analysis report: {report}")

    def offline_exit() -> int:
        zip_path = zip_fallback(dump_dir)
        print(f"\noffline bundle: {zip_path}")
        print("Attach it to a GitHub issue on the repo (drag & drop in the browser);")
        print("an AI agent session can then download the attachment, unzip it, and run")
        print("scripts/analyze_pipeline_timing.py on it.")
        return 0

    if args.offline:
        return offline_exit()
    try:
        import mlflow
    except ImportError:
        print("mlflow not installed (pip install mlflow) — falling back to zip.")
        return offline_exit()

    tracking_uri = os.environ.get("MLFLOW_TRACKING_URI", "https://mlflow.yofo.bio")
    mlflow.set_tracking_uri(tracking_uri)
    try:
        mlflow.set_experiment(args.experiment_name)
    except Exception as e:  # unreachable server, bad creds, ...
        print(f"MLflow unreachable ({e}) — falling back to zip.")
        return offline_exit()

    last = rows[-1]
    run_name = args.run_name or f"{socket.gethostname()}-{dump_dir.name}"
    with mlflow.start_run(run_name=run_name) as run:
        mlflow.log_params({
            "host": socket.gethostname(),
            "platform": platform.platform(),
            "realtime_mode": "batch" if last.get("realtime_mode") == 1 else "inline",
            "drop_frames": bool(last.get("drop_frames")),
            "experiment_active": any(r.get("experiment_active") == 1 for r in rows),
            "duration_s": round(rows[-1]["t_s"] - rows[0]["t_s"], 1),
            "trend_samples": len(rows),
        })
        for kv in args.tag:
            key, _, value = kv.partition("=")
            mlflow.set_tag(key, value)

        for column in SERIES_COLUMNS:
            for minute, values in apt.window_series(rows, column):
                mlflow.log_metric(column, apt.median(values), step=minute)
        for column in SERIES_COLUMNS:
            metric = apt.TrendMetric(rows, column)
            if metric.ok and metric.ratio != float("inf"):
                mlflow.log_metric(f"{column}_steady_ratio", metric.ratio)

        mlflow.log_artifacts(str(dump_dir))
        print(f"\nMLflow run: {tracking_uri}/#/experiments/"
              f"{run.info.experiment_id}/runs/{run.info.run_id}")
        print("Reviewers (human or AI agent) can fetch the artifacts and metric")
        print("series via the MLflow UI or REST API using the same credentials.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
