#!/usr/bin/env python3
"""Generate the KIN-11 review bundle from the HF dataset pipeline harness."""

from __future__ import annotations

import datetime as dt
import html
import json
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
ARTIFACT_DIR = REPO_ROOT / "artifacts" / "kin-5" / "hf_dataset_pipeline"
REVIEW_DIR = REPO_ROOT / "artifacts" / "review" / "KIN-11"
LOG_DIR = REVIEW_DIR / "logs"
SAMPLES_DIR = REVIEW_DIR / "samples"


def now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).astimezone().isoformat(timespec="seconds")


def git_short_sha() -> str:
    return subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=REPO_ROOT,
        text=True,
    ).strip()


def run_command(command_id: str, argv: list[str]) -> dict:
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_path = LOG_DIR / f"{command_id}.log"
    started_at = now_iso()
    command_text = " ".join(argv)

    with log_path.open("w", encoding="utf-8") as log:
        log.write(f"command: {command_text}\n")
        log.write(f"workdir: {REPO_ROOT}\n")
        log.write(f"commit: {git_short_sha()}\n")
        log.write(f"started_at: {started_at}\n")
        log.flush()

        process = subprocess.run(
            argv,
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        log.write(process.stdout)
        finished_at = now_iso()
        log.write(f"finished_at: {finished_at}\n")
        log.write(f"exit_code: {process.returncode}\n")

    return {
        "id": command_id,
        "command": command_text,
        "argv": argv,
        "exit_code": process.returncode,
        "started_at": started_at,
        "finished_at": finished_at,
        "log": rel(log_path),
        "log_path": log_path,
    }


def rel(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def report_rel(path: Path) -> str:
    return path.relative_to(REVIEW_DIR).as_posix()


def read_metrics() -> dict:
    metrics_path = ARTIFACT_DIR / "metrics.json"
    if not metrics_path.exists():
        raise FileNotFoundError(f"missing metrics JSON: {metrics_path}")
    return json.loads(metrics_path.read_text(encoding="utf-8"))


def copy_review_inputs(metrics: dict) -> tuple[Path, list[dict]]:
    SAMPLES_DIR.mkdir(parents=True, exist_ok=True)
    copied_samples: list[dict] = []

    for sample in metrics.get("samples", []):
        copied = dict(sample)
        for key, suffix in (
            ("input_path", "input"),
            ("mask_path", "mask"),
            ("overlay_path", "overlay"),
        ):
            source = Path(sample[key])
            if not source.is_absolute():
                source = REPO_ROOT / source
            target = SAMPLES_DIR / f"{sample['sample_id']}-{suffix}.png"
            shutil.copy2(source, target)
            copied[key] = rel(target)
        copied_samples.append(copied)

    review_metrics = dict(metrics)
    review_metrics["samples"] = copied_samples
    review_metrics_path = REVIEW_DIR / "metrics.json"
    review_metrics_path.write_text(
        json.dumps(review_metrics, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return review_metrics_path, copied_samples


def write_flow_diagram() -> Path:
    path = REVIEW_DIR / "flow_diagram.svg"
    path.write_text(
        """<svg xmlns="http://www.w3.org/2000/svg" width="1160" height="430" viewBox="0 0 1160 430" role="img" aria-label="KIN-11 HF artifact flow">
  <style>
    .box { fill: #f8fafc; stroke: #334155; stroke-width: 2; rx: 8; }
    .evidence { fill: #ecfdf5; stroke: #047857; stroke-width: 2; rx: 8; }
    .text { font: 16px Arial, sans-serif; fill: #0f172a; }
    .small { font: 13px Arial, sans-serif; fill: #334155; }
    .arrow { stroke: #475569; stroke-width: 2.5; marker-end: url(#arrow); fill: none; }
  </style>
  <defs>
    <marker id="arrow" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto">
      <path d="M2,2 L10,6 L2,10 z" fill="#475569"/>
    </marker>
  </defs>
  <rect class="box" x="30" y="70" width="180" height="92"/>
  <text class="text" x="52" y="108">HF Dataset Viewer</text>
  <text class="small" x="52" y="132">stable row sample set</text>

  <rect class="box" x="270" y="70" width="190" height="92"/>
  <text class="text" x="293" y="108">Downloaded Frames</text>
  <text class="small" x="293" y="132">cache + manifest TSV</text>

  <rect class="box" x="520" y="70" width="210" height="92"/>
  <text class="text" x="548" y="108">Batch Pipeline Test</text>
  <text class="small" x="548" y="132">ProcessingService workers</text>

  <rect class="evidence" x="790" y="45" width="310" height="142"/>
  <text class="text" x="820" y="84">artifacts/kin-5</text>
  <text class="small" x="820" y="110">input frame, mask, overlay</text>
  <text class="small" x="820" y="134">metrics.json keyed by sample ID</text>
  <text class="small" x="820" y="158">README + dataset provenance</text>

  <rect class="evidence" x="790" y="260" width="310" height="112"/>
  <text class="text" x="820" y="300">artifacts/review/KIN-11</text>
  <text class="small" x="820" y="326">report.html + manifest.json</text>
  <text class="small" x="820" y="350">logs, sample gallery, flow diagram</text>

  <path class="arrow" d="M210 116 H270"/>
  <path class="arrow" d="M460 116 H520"/>
  <path class="arrow" d="M730 116 H790"/>
  <path class="arrow" d="M945 187 V260"/>
</svg>
""",
        encoding="utf-8",
    )
    return path


def file_size(path: Path) -> int:
    return path.stat().st_size if path.exists() else 0


def artifact_entry(path: Path, purpose: str, command_id: str | None = None, sample_id: str | None = None) -> dict:
    return {
        "path": rel(path),
        "size_bytes": file_size(path),
        "purpose": purpose,
        "sample_id": sample_id,
        "command_id": command_id,
        "linear_url": None,
    }


def write_report(
    commands: list[dict],
    metrics: dict,
    samples: list[dict],
    flow_path: Path,
    manifest_path: Path,
) -> Path:
    report_path = REVIEW_DIR / "report.html"
    failures = metrics.get("failures", [])
    verdict = "PASS" if all(command["exit_code"] == 0 for command in commands) and not failures else "FAIL"
    command_rows = "\n".join(
        f"<tr><td>{html.escape(command['id'])}</td><td><code>{html.escape(command['command'])}</code></td>"
        f"<td>{command['exit_code']}</td><td>{html.escape(command['started_at'])}</td>"
        f"<td>{html.escape(command['finished_at'])}</td><td><a href=\"{html.escape(report_rel(command['log_path']))}\">log</a></td></tr>"
        for command in commands
    )
    sample_rows = "\n".join(
        f"<tr><td>{html.escape(sample['sample_id'])}</td><td>{sample['row_index']}</td>"
        f"<td>{html.escape(sample['case_type'])}</td><td>{sample['mask_pixels']}</td>"
        f"<td>{sample['contour_count']}</td><td>{sample['area']:.3f}</td>"
        f"<td>{sample['deformability']:.6f}</td><td>{str(sample['is_valid']).lower()}</td></tr>"
        for sample in samples
    )
    gallery = "\n".join(
        f"<section class=\"sample\"><h3>{html.escape(sample['sample_id'])} - {html.escape(sample['case_type'])}</h3>"
        f"<figure><img src=\"{html.escape(report_rel(REPO_ROOT / sample['input_path']))}\" alt=\"{html.escape(sample['sample_id'])} input\"><figcaption>input frame</figcaption></figure>"
        f"<figure><img src=\"{html.escape(report_rel(REPO_ROOT / sample['mask_path']))}\" alt=\"{html.escape(sample['sample_id'])} mask\"><figcaption>processed mask</figcaption></figure>"
        f"<figure><img src=\"{html.escape(report_rel(REPO_ROOT / sample['overlay_path']))}\" alt=\"{html.escape(sample['sample_id'])} overlay\"><figcaption>contour overlay</figcaption></figure>"
        "</section>"
        for sample in samples
    )
    limitations = "No unresolved limitations. The HF Dataset Viewer dependency is public and does not require HF_TOKEN."
    if failures:
        limitations = "Regression failures are listed in metrics.json and must be resolved before review."

    report_path.write_text(
        f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>KIN-11 Recorded Image Verification Artifacts</title>
  <style>
    body {{ font-family: Arial, sans-serif; margin: 28px; color: #111827; background: #ffffff; }}
    h1, h2, h3 {{ color: #0f172a; }}
    code {{ background: #f1f5f9; padding: 2px 4px; border-radius: 4px; }}
    table {{ border-collapse: collapse; width: 100%; margin: 12px 0 24px; }}
    th, td {{ border: 1px solid #cbd5e1; padding: 8px; text-align: left; vertical-align: top; }}
    th {{ background: #e2e8f0; }}
    .verdict {{ display: inline-block; padding: 6px 10px; border-radius: 6px; background: {"#dcfce7" if verdict == "PASS" else "#fee2e2"}; font-weight: 700; }}
    .sample {{ display: grid; grid-template-columns: 180px repeat(3, minmax(180px, 1fr)); gap: 12px; align-items: start; margin-bottom: 24px; }}
    .sample h3 {{ margin: 0; font-size: 15px; }}
    figure {{ margin: 0; }}
    img {{ max-width: 100%; border: 1px solid #cbd5e1; background: #f8fafc; image-rendering: pixelated; }}
    figcaption {{ font-size: 13px; color: #475569; margin-top: 4px; }}
  </style>
</head>
<body>
  <h1>KIN-11 Recorded Image Verification Artifacts</h1>
  <p class="verdict">Summary verdict: {verdict}</p>
  <p>Issue: KIN-11. Commit: <code>{html.escape(git_short_sha())}</code>. CI status: local validation only; no remote CI link was available while generating this bundle.</p>

  <h2>Command Table</h2>
  <table>
    <thead><tr><th>ID</th><th>Command</th><th>Exit</th><th>Started</th><th>Finished</th><th>Log</th></tr></thead>
    <tbody>{command_rows}</tbody>
  </table>

  <h2>Build And Compile</h2>
  <p>The backend-only CMake configure and target build ran through the commands above. Exit codes are captured in the linked logs.</p>

  <h2>Tests</h2>
  <p>The HF dataset integration test writes durable artifacts to <code>{html.escape(rel(ARTIFACT_DIR))}</code> and verifies sample dimensions, masks, contours, and metric ranges.</p>

  <h2>PR Feedback Sweep</h2>
  <p>No PR existed when this bundle was generated. The final handoff sweep should inspect top-level comments, inline comments, review summaries, and checks after the PR is opened.</p>

  <h2>Flow Diagram</h2>
  <p><a href="{html.escape(report_rel(flow_path))}">Open SVG</a></p>
  <img src="{html.escape(report_rel(flow_path))}" alt="HF dataset artifact flow">

  <h2>Visual Sample Gallery</h2>
  {gallery}

  <h2>Metrics Table</h2>
  <table>
    <thead><tr><th>Sample ID</th><th>Row</th><th>Case</th><th>Mask Pixels</th><th>Contours</th><th>Area</th><th>Deformability</th><th>Valid</th></tr></thead>
    <tbody>{sample_rows}</tbody>
  </table>

  <h2>Known Limitations And Confusions</h2>
  <p>{html.escape(limitations)}</p>

  <h2>Regeneration</h2>
  <pre><code>python3 tools/kin11_generate_review_bundle.py</code></pre>
  <p>Machine-readable manifest: <a href="{html.escape(report_rel(manifest_path))}">manifest.json</a>. Metrics: <a href="metrics.json">metrics.json</a>.</p>
</body>
</html>
""",
        encoding="utf-8",
    )
    return report_path


def write_manifest(
    commands: list[dict],
    report_path: Path,
    metrics_path: Path,
    flow_path: Path,
    samples: list[dict],
) -> Path:
    manifest_path = REVIEW_DIR / "manifest.json"
    artifacts = [
        artifact_entry(report_path, "Canonical human-readable review report", "generate-report"),
        artifact_entry(manifest_path, "Machine-readable artifact index", "generate-report"),
        artifact_entry(flow_path, "Pipeline and evidence capture flow diagram", "generate-report"),
        artifact_entry(metrics_path, "Per-sample and aggregate metrics keyed by stable sample ID", "test"),
        artifact_entry(REPO_ROOT / "tools" / "kin11_generate_review_bundle.py", "Regeneration script", None),
        artifact_entry(ARTIFACT_DIR / "README.md", "Durable HF artifact README", "test"),
    ]

    for command in commands:
        artifacts.append(
            artifact_entry(command["log_path"], f"Command log for {command['id']}", command["id"])
        )

    for sample in samples:
        sample_id = sample["sample_id"]
        artifacts.extend(
            [
                artifact_entry(REPO_ROOT / sample["input_path"], "HF input frame", "test", sample_id),
                artifact_entry(REPO_ROOT / sample["mask_path"], "Processed mask image", "test", sample_id),
                artifact_entry(REPO_ROOT / sample["overlay_path"], "Contour overlay image", "test", sample_id),
            ]
        )

    manifest = {
        "issue": "KIN-11",
        "generated_at": now_iso(),
        "commit": git_short_sha(),
        "artifact_root": rel(REVIEW_DIR),
        "source_artifact_root": rel(ARTIFACT_DIR),
        "commands": [
            {
                "id": command["id"],
                "command": command["command"],
                "exit_code": command["exit_code"],
                "started_at": command["started_at"],
                "finished_at": command["finished_at"],
                "log": command["log"],
            }
            for command in commands
        ],
        "samples": [
            {
                "sample_id": sample["sample_id"],
                "row_index": sample["row_index"],
                "case_type": sample["case_type"],
                "input_path": sample["input_path"],
                "mask_path": sample["mask_path"],
                "overlay_path": sample["overlay_path"],
                "metrics_key": sample["sample_id"],
            }
            for sample in samples
        ],
        "artifacts": artifacts,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    manifest_repo_path = rel(manifest_path)
    for _ in range(3):
        current_size = file_size(manifest_path)
        for artifact in manifest["artifacts"]:
            if artifact["path"] == manifest_repo_path:
                artifact["size_bytes"] = current_size
                break
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if file_size(manifest_path) == current_size:
            break
    return manifest_path


def main() -> int:
    REVIEW_DIR.mkdir(parents=True, exist_ok=True)
    commands = [
        run_command("configure", ["cmake", "--preset", "linux-backend-only"]),
        run_command(
            "build",
            [
                "cmake",
                "--build",
                "--preset",
                "linux-backend-only-build",
                "--target",
                "kin10_hf_dataset_pipeline_test",
            ],
        ),
        run_command(
            "test",
            [
                "ctest",
                "--preset",
                "linux-backend-only-test",
                "-R",
                "backend.kin10_hf_dataset_pipeline",
                "--output-on-failure",
            ],
        ),
    ]

    metrics = read_metrics()
    metrics_path, samples = copy_review_inputs(metrics)
    flow_path = write_flow_diagram()
    placeholder_manifest = REVIEW_DIR / "manifest.json"
    report_path = write_report(commands, metrics, samples, flow_path, placeholder_manifest)
    manifest_path = write_manifest(commands, report_path, metrics_path, flow_path, samples)
    write_report(commands, metrics, samples, flow_path, manifest_path)

    failed = [command for command in commands if command["exit_code"] != 0]
    if failed or metrics.get("failures"):
        return 1

    print(f"KIN-11 review bundle: {rel(REVIEW_DIR)}")
    print(f"Report: {rel(report_path)}")
    print(f"Manifest: {rel(manifest_path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
