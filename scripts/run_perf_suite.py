"""Run the MIB-Studio performance test suite as a single tool.

This is the one-stop driver: it builds (optional), runs every perf
test, collects every JSON report, prints a compact human-readable
summary, and optionally uploads to MLflow.

No more opening five terminals. Typical usage:

    # Run everything in ./build (default), print summary to stdout:
    python scripts/run_perf_suite.py

    # Run only the portable subset (skip tests pulling Coremor/EGrabber):
    python scripts/run_perf_suite.py --only framestore processing hdf5

    # Re-aggregate already-generated JSON without re-running:
    python scripts/run_perf_suite.py --no-run

    # Run + upload to MLflow in one command:
    python scripts/run_perf_suite.py --upload --tag ci=local

    # Pick a different build dir / config:
    python scripts/run_perf_suite.py --build-dir build --config Release

Output files (written to --build-dir):

    thread_perf_results.json
    framestore_perf_results.json
    processing_perf_results.json
    hdf5_perf_results.json
    capture_processing_results.json
    perf_summary.md      # human-readable rollup, all benches in one place
    perf_summary.json    # machine-readable combined report

The script exits non-zero if any test fails (non-zero exit code or
sanity-invariant failure). MLflow upload is delegated to
`scripts/upload_perf_results.py`.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional


# ---- Test registry -----------------------------------------------------------

@dataclass(frozen=True)
class PerfTest:
    # Short name used on the CLI (--only/--skip), in summary tables, and
    # as the namespace for MLflow metrics.
    name: str
    # The ctest/executable name CMake registers.
    binary: str
    # Path of the JSON artefact (relative to the build dir).
    json_name: str
    # Env var the binary reads for the JSON output path. The CTest
    # ENVIRONMENT property sets this to an absolute build-dir path, but
    # when invoking the binary directly we set it ourselves.
    json_env: str
    # One-line description for the summary header.
    description: str


TESTS: List[PerfTest] = [
    PerfTest("thread",
             "thread_perf_test",
             "thread_perf_results.json",
             "MIB_THREAD_PERF_JSON",
             "TriggerService + AutofocusService latency"),
    PerfTest("framestore",
             "framestore_perf_test",
             "framestore_perf_results.json",
             "MIB_FRAMESTORE_PERF_JSON",
             "FrameStore push/get latency + contention"),
    PerfTest("processing",
             "processing_perf_test",
             "processing_perf_results.json",
             "MIB_PROCESSING_PERF_JSON",
             "computeProcessedFrame throughput sweep"),
    PerfTest("hdf5",
             "hdf5_perf_test",
             "hdf5_perf_results.json",
             "MIB_HDF5_PERF_JSON",
             "Hdf5Service appendFrames / recording throughput"),
    PerfTest("capture",
             "capture_processing_test",
             "capture_processing_results.json",
             "MIB_CAPTURE_PERF_JSON",
             "End-to-end mock-camera metric plumbing"),
]


# ---- Helpers -----------------------------------------------------------------

def locate_binary(build_dir: Path, binary: str, config: str) -> Optional[Path]:
    """CMake puts binaries in different places depending on generator /
    platform. Search the common locations."""
    ext = ".exe" if os.name == "nt" else ""
    candidates = [
        build_dir / config / f"{binary}{ext}",   # VS multi-config
        build_dir / f"{binary}{ext}",            # Ninja / single-config
        build_dir / "bin" / f"{binary}{ext}",    # some layouts
    ]
    for c in candidates:
        if c.is_file():
            return c
    return None


def run_one(test: PerfTest, build_dir: Path, config: str, extra_env: Dict[str, str],
            verbose: bool) -> Dict[str, object]:
    """Run a single perf test binary. Returns a dict describing the
    outcome (status, returncode, stdout/stderr tail on failure)."""
    info: Dict[str, object] = {"name": test.name, "status": "unknown"}

    exe = locate_binary(build_dir, test.binary, config)
    if exe is None:
        info["status"] = "binary_not_found"
        info["searched"] = [
            str(build_dir / config / test.binary),
            str(build_dir / test.binary),
        ]
        return info

    json_path = build_dir / test.json_name
    env = os.environ.copy()
    env.update(extra_env)
    env[test.json_env] = str(json_path)
    # capture_processing_test also needs mock-camera mode.
    if test.name == "capture":
        env.setdefault("MIB_CAMERA_MODE", "mock")

    print(f"--- {test.name:<11} | {exe.name} ---")
    proc = subprocess.run(
        [str(exe)],
        cwd=str(build_dir),
        env=env,
        stdout=None if verbose else subprocess.PIPE,
        stderr=None if verbose else subprocess.STDOUT,
        text=True,
    )
    info["returncode"] = proc.returncode
    if proc.returncode != 0:
        info["status"] = "failed"
        if not verbose and proc.stdout:
            # Capture only the tail so a broken test doesn't flood the summary.
            tail = "\n".join(proc.stdout.splitlines()[-40:])
            info["stderr_tail"] = tail
            print(tail)
    else:
        info["status"] = "ok"
    info["json_path"] = str(json_path) if json_path.exists() else None
    return info


def load_json(path: Path) -> Optional[Dict]:
    try:
        with path.open("r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return None
    except Exception as ex:
        print(f"WARN: failed to parse {path}: {ex}", file=sys.stderr)
        return None


# ---- Summary rendering -------------------------------------------------------

def fmt_num(x: Optional[float], digits: int = 2) -> str:
    if x is None:
        return "—"
    if isinstance(x, (int,)) or (isinstance(x, float) and x.is_integer()):
        return f"{int(x)}"
    return f"{x:.{digits}f}"


def summarise_latency_bench(key: str, stats: Dict) -> str:
    """One-line summary for a latency bench (has n/median/p99/max)."""
    return (f"  {key:<45} "
            f"n={fmt_num(stats.get('n'))} "
            f"median={fmt_num(stats.get('median_us'), 2)} us "
            f"p99={fmt_num(stats.get('p99_us'), 2)} us "
            f"max={fmt_num(stats.get('max_us'), 2)} us")


def render_thread(report: Dict) -> List[str]:
    lines = []
    for k in ("on_ring_ratio_latency",
              "trigger_wakeup_idle",
              "trigger_wakeup_ring_ratio_load",
              "trigger_wakeup_ui_snapshot_sim"):
        if k in report and isinstance(report[k], dict):
            lines.append(summarise_latency_bench(k, report[k]))
    return lines


def render_framestore(report: Dict) -> List[str]:
    lines = []
    # Latency benches
    for k, v in report.items():
        if isinstance(v, dict) and "p99_us" in v:
            lines.append(summarise_latency_bench(k, v))
    # Contention throughput
    if "contention_wall_ms" in report:
        pushed = report.get("contention_pushed_count", 0)
        gotten = report.get("contention_gotten_count", 0)
        wall = report.get("contention_wall_ms", 0) or 1.0
        lines.append(f"  contention 1024x1024 | "
                     f"push_tput={pushed * 1000.0 / wall:,.0f}/s "
                     f"get_tput={gotten * 1000.0 / wall:,.0f}/s "
                     f"wall={wall:.0f}ms")
    return lines


def render_processing(report: Dict) -> List[str]:
    lines = []
    keys = sorted(k for k in report if k.startswith("compute_") and "implied_fps" not in k)
    for k in keys:
        v = report.get(k, {})
        fps = report.get(f"{k}_implied_fps")
        if isinstance(v, dict):
            lines.append(summarise_latency_bench(k, v)
                         + f"  (~{fmt_num(fps, 0)} fps)")
    return lines


def render_hdf5(report: Dict) -> List[str]:
    lines = []
    if report.get("status") == "skipped_no_hdf5":
        return ["  (skipped — HDF5 lib not available at runtime)"]
    for prefix in ("append_frames_batch_10",
                   "append_frames_batch_100",
                   "append_frames_batch_1000",
                   "append_frames_sustained",
                   "append_recording"):
        fps = report.get(f"{prefix}_frames_per_s")
        mbs = report.get(f"{prefix}_mb_per_s")
        wall = report.get(f"{prefix}_wall_ms")
        if fps is None and mbs is None:
            continue
        lines.append(f"  {prefix:<35} "
                     f"{fmt_num(fps, 0):>8}/s  "
                     f"{fmt_num(mbs, 1):>7} MB/s  "
                     f"wall={fmt_num(wall, 1)}ms")
    return lines


def render_capture(report: Dict) -> List[str]:
    lines = []
    elapsed = report.get("elapsed_sec", 0)
    fp = report.get("frames_processed", 0)
    fps = report.get("wall_clock_fps", 0)
    lines.append(f"  elapsed={fmt_num(elapsed, 2)}s "
                 f"frames_processed={fmt_num(fp)} "
                 f"wall_fps={fmt_num(fps, 1)}")
    lines.append(f"  algo_fps_1s={fmt_num(report.get('algo_fps_1s'), 1)} "
                 f"algo_avg_us_1s={fmt_num(report.get('algo_avg_us_1s'), 1)} "
                 f"total_valid_flushed={fmt_num(report.get('total_valid_flushed'))}")
    lines.append(f"  sanity_failures={fmt_num(report.get('sanity_failures'))}")
    return lines


RENDERERS = {
    "thread": render_thread,
    "framestore": render_framestore,
    "processing": render_processing,
    "hdf5": render_hdf5,
    "capture": render_capture,
}


def build_summary(build_dir: Path, run_results: List[Dict]) -> str:
    out: List[str] = []
    out.append("# MIB-Studio Perf Suite Summary")
    out.append("")
    out.append(f"Build dir: `{build_dir}`")
    out.append("")
    for test in TESTS:
        result = next((r for r in run_results if r["name"] == test.name), None)
        status = result["status"] if result else "not_run"
        marker = {"ok": "OK", "failed": "FAIL", "not_run": "SKIP",
                  "binary_not_found": "MISSING"}.get(status, status)
        out.append(f"## [{marker}] {test.name} — {test.description}")
        if result and result.get("returncode") is not None and result["returncode"] != 0:
            out.append(f"  (returncode={result['returncode']})")
        json_path = build_dir / test.json_name
        report = load_json(json_path)
        if report is None:
            out.append("  (no JSON report found)")
        else:
            renderer = RENDERERS.get(test.name)
            rendered = renderer(report) if renderer else []
            if not rendered:
                out.append("  (JSON present but no known metrics to render)")
            else:
                out.extend(rendered)
        out.append("")
    return "\n".join(out)


def build_combined_json(build_dir: Path) -> Dict:
    """Aggregate every test's JSON into a single dict, namespaced by
    test short name."""
    combined: Dict[str, object] = {}
    for test in TESTS:
        report = load_json(build_dir / test.json_name)
        if report is not None:
            combined[test.name] = report
    return combined


# ---- Main --------------------------------------------------------------------

def parse_args(argv: List[str]) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", type=Path, default=Path("build"),
                    help="CMake build directory (default: ./build)")
    ap.add_argument("--config", default="Release",
                    help="Build config for multi-config generators (default: Release)")
    all_names = [t.name for t in TESTS]
    ap.add_argument("--only", nargs="+", choices=all_names, default=None,
                    help="Run only these tests (space-separated).")
    ap.add_argument("--skip", nargs="+", choices=all_names, default=[],
                    help="Skip these tests.")
    ap.add_argument("--no-run", action="store_true",
                    help="Don't run the binaries; just aggregate existing JSON.")
    ap.add_argument("--verbose", "-v", action="store_true",
                    help="Stream each test's stdout live (noisy).")
    ap.add_argument("--upload", action="store_true",
                    help="After the run, invoke scripts/upload_perf_results.py on the JSONs.")
    ap.add_argument("--tag", action="append", default=[],
                    help="Passed through to the uploader. Repeatable.")
    ap.add_argument("--experiment", default="mib-studio-perf",
                    help="MLflow experiment name (passed to uploader).")
    ap.add_argument("--run-name", default=None,
                    help="MLflow run name (passed to uploader).")
    ap.add_argument("--dry-run-upload", action="store_true",
                    help="Pass --dry-run to the uploader.")
    return ap.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)

    build_dir: Path = args.build_dir.resolve()
    if not build_dir.is_dir():
        print(f"Build dir not found: {build_dir}", file=sys.stderr)
        return 2

    selected = [t for t in TESTS
                if (args.only is None or t.name in args.only)
                and t.name not in args.skip]
    if not selected:
        print("No tests selected after --only/--skip filters.", file=sys.stderr)
        return 2

    run_results: List[Dict] = []
    if args.no_run:
        for t in selected:
            run_results.append({
                "name": t.name, "status": "ok", "returncode": 0,
                "json_path": str(build_dir / t.json_name),
            })
    else:
        for t in selected:
            r = run_one(t, build_dir, args.config, extra_env={}, verbose=args.verbose)
            run_results.append(r)

    # Always rebuild + write the summary, even on failure, so the user
    # sees partial results.
    summary_md = build_summary(build_dir, run_results)
    summary_md_path = build_dir / "perf_summary.md"
    summary_md_path.write_text(summary_md, encoding="utf-8")

    combined = build_combined_json(build_dir)
    summary_json_path = build_dir / "perf_summary.json"
    summary_json_path.write_text(json.dumps(combined, indent=2), encoding="utf-8")

    print()
    print(summary_md)
    print()
    print(f"Summary written to:\n  {summary_md_path}\n  {summary_json_path}")

    any_failed = any(r.get("status") == "failed" for r in run_results)

    if args.upload:
        uploader = Path(__file__).with_name("upload_perf_results.py")
        if not uploader.is_file():
            print(f"Uploader not found at {uploader}", file=sys.stderr)
            return 3
        cmd = [sys.executable, str(uploader)]
        for t in selected:
            jp = build_dir / t.json_name
            if jp.exists():
                cmd.extend(["--json", str(jp)])
        if args.experiment:
            cmd.extend(["--experiment", args.experiment])
        if args.run_name:
            cmd.extend(["--run-name", args.run_name])
        for tag in args.tag:
            cmd.extend(["--tag", tag])
        if args.dry_run_upload:
            cmd.append("--dry-run")
        print("\nUploading to MLflow...")
        print("  " + " ".join(cmd))
        rc = subprocess.call(cmd)
        if rc != 0:
            print(f"Uploader exited {rc}", file=sys.stderr)
            return rc

    return 1 if any_failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
