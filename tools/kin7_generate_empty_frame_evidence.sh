#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-review_artifacts/KIN-7}"
BINARY="${2:-build/linux-backend/kin7_empty_frame_evidence}"
REPORT_ONLY="${KIN7_REPORT_ONLY:-0}"

mkdir -p "${OUT_DIR}" "${OUT_DIR}/logs" "${OUT_DIR}/hf_images"

if [[ "${REPORT_ONLY}" != "1" ]]; then
  rm -rf "${OUT_DIR}/samples"
  rm -f "${OUT_DIR}/metrics.json" \
        "${OUT_DIR}/report.html" \
        "${OUT_DIR}/manifest.json" \
        "${OUT_DIR}/flow_diagram.svg" \
        "${OUT_DIR}/background_mean.png"

  python3 - "${OUT_DIR}" <<'PY'
import json
import pathlib
import sys
import time
import urllib.parse
import urllib.request

out_dir = pathlib.Path(sys.argv[1])
image_dir = out_dir / "hf_images"
image_dir.mkdir(parents=True, exist_ok=True)

dataset = "gavinlouuu/512x96stream"
config = "default"
split = "train"
base_url = "https://datasets-server.huggingface.co"
page_size = 100


def api_url(path, params):
    return f"{base_url}{path}?{urllib.parse.urlencode(params)}"


def fetch_bytes(url, retries=5):
    last_error = None
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read()
        except Exception as exc:  # noqa: BLE001 - URL context is more useful here.
            last_error = exc
            time.sleep(min(10, 2**attempt))
    raise RuntimeError(f"failed after {retries} attempts: {url}: {last_error}")


def fetch_json(url):
    return json.loads(fetch_bytes(url).decode("utf-8"))


splits = fetch_json(api_url("/splits", {"dataset": dataset}))
size = fetch_json(api_url("/size", {"dataset": dataset}))
(out_dir / "hf_splits.json").write_text(json.dumps(splits, indent=2, sort_keys=True) + "\n", encoding="utf-8")
(out_dir / "hf_size.json").write_text(json.dumps(size, indent=2, sort_keys=True) + "\n", encoding="utf-8")

row_count = None
for item in size.get("size", {}).get("splits", []):
    if item.get("config") == config and item.get("split") == split:
        row_count = int(item["num_rows"])
        break
if row_count is None:
    raise SystemExit(f"could not resolve row count for {dataset}/{config}/{split}")

manifest_path = out_dir / "hf_sample_manifest.tsv"
rows_path = out_dir / "hf_rows.jsonl"
downloads_path = out_dir / "hf_image_downloads.tsv"

with manifest_path.open("w", encoding="utf-8") as manifest, \
        rows_path.open("w", encoding="utf-8") as rows_out, \
        downloads_path.open("w", encoding="utf-8") as downloads:
    manifest.write("row_idx\tpath\twidth\theight\tcase_type\n")
    downloads.write("row_idx\tpath\tbytes\tstatus\tsrc\n")
    for offset in range(0, row_count, page_size):
        length = min(page_size, row_count - offset)
        try:
            page = fetch_json(api_url(
                "/rows",
                {
                    "dataset": dataset,
                    "config": config,
                    "split": split,
                    "offset": offset,
                    "length": length,
                },
            ))
            rows = page.get("rows", [])
            if not rows:
                raise RuntimeError(f"Dataset Viewer returned no rows at offset {offset}")
        except Exception as exc:  # noqa: BLE001 - fallback is recorded in provenance.
            print(f"WARNING: rows API fallback at offset {offset}: {exc}", file=sys.stderr)
            rows = [
                {
                    "row_idx": row_index,
                    "row": {
                        "image": {
                            "src": "",
                            "width": 512,
                            "height": 96,
                        }
                    },
                    "fallback_reason": str(exc),
                }
                for row_index in range(offset, offset + length)
            ]
        for row in rows:
            row_index = int(row["row_idx"])
            image = row.get("row", {}).get("image", {})
            src = image.get("src") or "cached-after-rows-api-fallback"
            width = int(image.get("width", 0))
            height = int(image.get("height", 0))
            if width <= 0 or height <= 0:
                raise SystemExit(f"row {row_index} does not contain image dimensions")
            image_path = image_dir / f"hf_row_{row_index:05d}.jpg"
            status = "cached"
            if not image_path.exists() or image_path.stat().st_size == 0:
                if src == "cached-after-rows-api-fallback":
                    raise SystemExit(f"row {row_index} missing cached image after rows API fallback")
                image_path.write_bytes(fetch_bytes(src))
                status = "downloaded"
            if row.get("fallback_reason"):
                status = "cached_rows_api_fallback"
            case_type = "hf-stream-row"
            if row_index == 0:
                case_type = "hf-first-row"
            elif row_index == row_count - 1:
                case_type = "hf-last-row"
            elif row_index in (row_count // 4, row_count // 2, (row_count * 3) // 4):
                case_type = "hf-quarter-sentinel"
            rows_out.write(json.dumps(
                {
                    "row_idx": row_index,
                    "src": src,
                    "width": width,
                    "height": height,
                    "case_type": case_type,
                },
                sort_keys=True,
            ) + "\n")
            manifest.write(f"{row_index}\t{image_path}\t{width}\t{height}\t{case_type}\n")
            downloads.write(f"{row_index}\t{image_path}\t{image_path.stat().st_size}\t{status}\t{src}\n")

print(f"HF dataset prepared: dataset={dataset} split={split} rows={row_count} manifest={manifest_path}")
PY

  if [[ ! -x "${BINARY}" ]]; then
    echo "KIN-7 evidence binary is not executable: ${BINARY}" >&2
    exit 1
  fi

  "${BINARY}" "${OUT_DIR}" "${OUT_DIR}/hf_sample_manifest.tsv"
fi

python3 - "${OUT_DIR}" "${BINARY}" <<'PY'
import html
import json
import os
import pathlib
import re
import sys
from datetime import datetime, timezone

out_dir = pathlib.Path(sys.argv[1])
binary = sys.argv[2]
logs_dir = out_dir / "logs"
metrics_path = out_dir / "metrics.json"
metrics = json.loads(metrics_path.read_text(encoding="utf-8"))


def rel(path):
    path = pathlib.Path(path)
    try:
        return path.relative_to(out_dir).as_posix()
    except ValueError:
        return path.as_posix()


def file_size(path):
    path = pathlib.Path(path)
    return path.stat().st_size if path.exists() else 0


def parse_log(path):
    info = {
        "path": rel(path),
        "command": "",
        "start": "",
        "end": "",
        "exit": "",
        "exists": pathlib.Path(path).exists(),
    }
    if not info["exists"]:
        return info
    text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        if line.startswith("command=") and not info["command"]:
            info["command"] = line.split("=", 1)[1]
        elif line.startswith("start=") and not info["start"]:
            info["start"] = line.split("=", 1)[1]
        elif line.startswith("end="):
            info["end"] = line.split("=", 1)[1]
        elif line.startswith("exit="):
            info["exit"] = line.split("=", 1)[1]
    return info


command_logs = [
    ("Reproduction", logs_dir / "reproduction.log"),
    ("Configure", logs_dir / "configure.log"),
    ("Build", logs_dir / "build.log"),
    ("Targeted tests", logs_dir / "test.log"),
    ("Full backend tests", logs_dir / "test-full.log"),
    ("HF/sample metrics", logs_dir / "sample-metrics.log"),
    ("PR feedback", logs_dir / "pr-feedback.log"),
    ("CI checks", logs_dir / "ci-checks.log"),
]
parsed_logs = [(label, parse_log(path)) for label, path in command_logs]

flow_svg = out_dir / "flow_diagram.svg"
flow_svg.write_text("""<svg xmlns="http://www.w3.org/2000/svg" width="1120" height="360" viewBox="0 0 1120 360">
  <style>
    .box { fill: #f7f9fb; stroke: #2f5d7c; stroke-width: 2; rx: 6; }
    .gate { fill: #fff7e8; stroke: #b26a00; stroke-width: 2; rx: 6; }
    .out { fill: #eef8ef; stroke: #2d7a3e; stroke-width: 2; rx: 6; }
    .text { font: 14px sans-serif; fill: #102331; }
    .small { font: 12px sans-serif; fill: #384b59; }
    .arrow { stroke: #304a5d; stroke-width: 2; marker-end: url(#arrow); fill: none; }
  </style>
  <defs>
    <marker id="arrow" markerWidth="10" markerHeight="10" refX="8" refY="3" orient="auto" markerUnits="strokeWidth">
      <path d="M0,0 L0,6 L9,3 z" fill="#304a5d" />
    </marker>
  </defs>
  <rect class="box" x="24" y="86" width="130" height="82"/>
  <text class="text" x="42" y="116">Gray frame</text><text class="small" x="42" y="140">512x96 ROI</text>
  <rect class="box" x="190" y="86" width="150" height="82"/>
  <text class="text" x="208" y="112">Background</text><text class="small" x="208" y="136">mean/manual</text><text class="small" x="208" y="154">shift search +/-3 px</text>
  <rect class="box" x="380" y="86" width="150" height="82"/>
  <text class="text" x="398" y="116">Diff + threshold</text><text class="small" x="398" y="140">energy and pixels</text>
  <rect class="gate" x="570" y="70" width="185" height="114"/>
  <text class="text" x="590" y="104">Pre-contour empty gate</text><text class="small" x="590" y="130">morph occupancy</text><text class="small" x="590" y="150">threshold sensitivity</text><text class="small" x="590" y="170">background shift metrics</text>
  <rect class="out" x="805" y="32" width="130" height="70"/>
  <text class="text" x="826" y="62">Discard</text><text class="small" x="826" y="84">zero mask</text>
  <rect class="box" x="805" y="148" width="130" height="70"/>
  <text class="text" x="828" y="178">Contours</text><text class="small" x="828" y="200">nested object</text>
  <rect class="out" x="980" y="148" width="116" height="70"/>
  <text class="text" x="1000" y="178">Metrics</text><text class="small" x="1000" y="200">valid/invalid</text>
  <path class="arrow" d="M154 127 H190"/>
  <path class="arrow" d="M340 127 H380"/>
  <path class="arrow" d="M530 127 H570"/>
  <path class="arrow" d="M755 103 C780 80,790 67,805 67"/>
  <path class="arrow" d="M755 151 C780 170,790 183,805 183"/>
  <path class="arrow" d="M935 183 H980"/>
</svg>
""", encoding="utf-8")

aggregate = metrics["aggregate"]
dataset = metrics["dataset"]
config = metrics["config"]
review_samples = metrics["review_samples"]

verdict = "PASS"
if not review_samples or aggregate["hf_non_empty_candidates"] == 0:
    verdict = "CHECK"

rows_html = []
for label, info in parsed_logs:
    rows_html.append(
        "<tr>"
        f"<td>{html.escape(label)}</td>"
        f"<td><code>{html.escape(info['command'] or 'n/a')}</code></td>"
        f"<td>{html.escape(info['exit'] or 'n/a')}</td>"
        f"<td>{html.escape(info['start'] or 'n/a')}</td>"
        f"<td>{html.escape(info['end'] or 'n/a')}</td>"
        f"<td><a href='{html.escape(info['path'])}'>{html.escape(info['path'])}</a></td>"
        "</tr>"
    )

sample_rows = []
gallery = []
for item in review_samples:
    m = item["metrics"]
    sample_rows.append(
        "<tr>"
        f"<td>{html.escape(m['sample_id'])}</td>"
        f"<td>{html.escape(m['case_type'])}</td>"
        f"<td>{html.escape(m['prediction'])}</td>"
        f"<td>{m['foreground_pixels']}</td>"
        f"<td>{m['morph_pixels']}</td>"
        f"<td>{m['roi_occupancy']:.4f}</td>"
        f"<td>({m['background_shift_x']},{m['background_shift_y']})</td>"
        f"<td>{'yes' if m['is_valid'] else 'no'}</td>"
        "</tr>"
    )
    gallery.append(
        "<section class='sample'>"
        f"<h3>{html.escape(m['sample_id'])} - {html.escape(m['case_type'])}</h3>"
        "<div class='images'>"
        f"<figure><img src='{html.escape(rel(item['input_path']))}'><figcaption>input</figcaption></figure>"
        f"<figure><img src='{html.escape(rel(item['mask_path']))}'><figcaption>mask/output</figcaption></figure>"
        f"<figure><img src='{html.escape(rel(item['overlay_path']))}'><figcaption>overlay/contours</figcaption></figure>"
        "</div></section>"
    )

commit = os.environ.get("KIN7_COMMIT", "")
pr_url = os.environ.get("KIN7_PR_URL", "")
ci_status = os.environ.get("KIN7_CI_STATUS", "pending/not captured")

report_html = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>KIN-7 Robust Empty-Frame Discarding Evidence</title>
<style>
body {{ font-family: Arial, sans-serif; margin: 24px; color: #14212b; }}
h1, h2, h3 {{ margin-bottom: 0.35rem; }}
table {{ border-collapse: collapse; width: 100%; margin: 12px 0 24px; }}
th, td {{ border: 1px solid #c8d3dc; padding: 6px 8px; text-align: left; vertical-align: top; }}
th {{ background: #eef3f7; }}
code {{ white-space: pre-wrap; }}
.verdict {{ display: inline-block; padding: 4px 8px; border-radius: 4px; background: #e7f7e7; border: 1px solid #7bb47b; }}
.images {{ display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; align-items: start; }}
figure {{ margin: 0; border: 1px solid #c8d3dc; padding: 6px; }}
img {{ max-width: 100%; image-rendering: pixelated; }}
figcaption {{ font-size: 12px; color: #455766; }}
.sample {{ margin-bottom: 20px; }}
</style>
</head>
<body>
<h1>KIN-7 Robust Empty-Frame Discarding Evidence</h1>
<p><span class="verdict">Verdict: {html.escape(verdict)}</span></p>
<h2>Summary</h2>
<p>Branch evidence for pre-contour empty-frame discard with shift-compensated background subtraction. HF dataset {html.escape(dataset['repo'])}/{html.escape(dataset['split'])} has {dataset['sample_count']} rows and no label column, so metrics use full-set classification counts plus a manual-review sample gallery.</p>
<table>
<tr><th>Issue</th><td>KIN-7</td></tr>
<tr><th>Commit</th><td>{html.escape(commit or 'pending')}</td></tr>
<tr><th>PR</th><td>{('<a href=\"' + html.escape(pr_url) + '\">' + html.escape(pr_url) + '</a>') if pr_url else 'pending'}</td></tr>
<tr><th>CI</th><td>{html.escape(ci_status)}</td></tr>
<tr><th>Generated</th><td>{datetime.now(timezone.utc).isoformat()}</td></tr>
</table>
<h2>Aggregate Metrics</h2>
<table>
<tr><th>HF rows</th><th>Empty discarded</th><th>Non-empty candidates</th><th>Valid object candidates</th><th>Invalid non-empty candidates</th><th>Manual samples</th></tr>
<tr><td>{dataset['sample_count']}</td><td>{aggregate['hf_empty_discarded']}</td><td>{aggregate['hf_non_empty_candidates']}</td><td>{aggregate['hf_valid_object_candidates']}</td><td>{aggregate['hf_invalid_non_empty_candidates']}</td><td>{aggregate['manual_review_sample_count']}</td></tr>
</table>
<h2>Command Table</h2>
<table><tr><th>Step</th><th>Command</th><th>Exit</th><th>Start</th><th>End</th><th>Log</th></tr>
{''.join(rows_html)}
</table>
<h2>Build And Tests</h2>
<p>Configure, build, targeted backend tests, full backend tests, sample classification, PR feedback sweep, and CI check logs are linked in the command table when available.</p>
<h2>Pipeline Flow</h2>
<p><a href="flow_diagram.svg">Open flow_diagram.svg</a></p>
<img src="flow_diagram.svg" alt="KIN-7 processing flow">
<h2>Config Defaults</h2>
<table><tr><th>Parameter</th><th>Value</th></tr>
{''.join(f'<tr><td>{html.escape(str(k))}</td><td>{html.escape(str(v))}</td></tr>' for k, v in config.items())}
</table>
<h2>Sample Metrics</h2>
<table><tr><th>Sample</th><th>Case</th><th>Prediction</th><th>Foreground px</th><th>Morph px</th><th>Occupancy</th><th>BG shift</th><th>Valid</th></tr>
{''.join(sample_rows)}
</table>
<h2>Visual Sample Gallery</h2>
{''.join(gallery)}
<h2>Known Limitations And Confusions</h2>
<ul>
<li>The HF stream is unlabeled, so precision/recall cannot be computed without a separate labeled review set.</li>
<li>Background alignment is intentionally bounded to small shifts and is not a replacement for a badly selected background.</li>
</ul>
<h2>Regeneration</h2>
<pre>cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build --target processing_batch_pipeline_test kin7_empty_frame_evidence
tools/kin7_generate_empty_frame_evidence.sh {html.escape(str(out_dir))} {html.escape(binary)}</pre>
</body>
</html>
"""
(out_dir / "report.html").write_text(report_html, encoding="utf-8")

artifacts = []


def add_artifact(path, purpose, command="", sample_id=None):
    path = pathlib.Path(path)
    if not path.exists():
        return
    artifacts.append({
        "path": rel(path),
        "local_path": path.as_posix(),
        "file_size": file_size(path),
        "purpose": purpose,
        "sample_id": sample_id,
        "command": command,
        "linear_url": None,
    })


add_artifact(out_dir / "report.html", "canonical human-readable review report")
add_artifact(flow_svg, "pipeline/data/control flow diagram")
add_artifact(metrics_path, "per-frame and aggregate empty-frame metrics")
add_artifact(out_dir / "background_mean.png", "mean HF background used for full-set classification")
add_artifact(out_dir / "hf_size.json", "HF dataset size provenance")
add_artifact(out_dir / "hf_splits.json", "HF dataset split provenance")
add_artifact(out_dir / "hf_rows.jsonl", "HF row download provenance")
add_artifact(out_dir / "hf_sample_manifest.tsv", "HF row to local image manifest")
add_artifact(out_dir / "hf_image_downloads.tsv", "HF image cache/download audit")
for label, info in parsed_logs:
    add_artifact(out_dir / info["path"], f"{label} command log", info["command"])
add_artifact(out_dir / "logs" / "ci-summary.json", "machine-readable GitHub check summary")
for item in review_samples:
    m = item["metrics"]
    add_artifact(item["input_path"], f"{m['sample_id']} input image", sample_id=m["sample_id"])
    add_artifact(item["mask_path"], f"{m['sample_id']} mask/output image", sample_id=m["sample_id"])
    add_artifact(item["overlay_path"], f"{m['sample_id']} overlay/contours image", sample_id=m["sample_id"])
script_path = pathlib.Path("tools/kin7_generate_empty_frame_evidence.sh")
if script_path.exists():
    add_artifact(script_path, "regeneration script")

manifest = {
    "issue": "KIN-7",
    "generated_at_utc": datetime.now(timezone.utc).isoformat(),
    "artifact_root": out_dir.as_posix(),
    "binary": binary,
    "summary": aggregate,
    "artifacts": artifacts,
}
manifest_path = out_dir / "manifest.json"
manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
artifacts.insert(1, {
    "path": rel(manifest_path),
    "local_path": manifest_path.as_posix(),
    "file_size": file_size(manifest_path),
    "purpose": "machine-readable artifact index",
    "sample_id": None,
    "command": "",
    "linear_url": None,
})
manifest["artifacts"] = artifacts
manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
print(f"Report: {out_dir / 'report.html'}")
print(f"Manifest: {manifest_path}")
PY
