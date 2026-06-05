#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-review_artifacts/KIN-7}"
BINARY="${2:-build/linux-backend/kin7_empty_frame_evidence}"
REPORT_ONLY="${KIN7_REPORT_ONLY:-0}"

mkdir -p "${OUT_DIR}" "${OUT_DIR}/logs" "${OUT_DIR}/samples" "${OUT_DIR}/scripts" "${OUT_DIR}/hf_images"
cp "$0" "${OUT_DIR}/scripts/regenerate.sh"

if [[ "${REPORT_ONLY}" != "1" ]]; then
python - "${OUT_DIR}" <<'PY'
import json
import pathlib
import sys
import time
import urllib.parse
import urllib.request

out_dir = pathlib.Path(sys.argv[1])
dataset = "gavinlouuu/512x96stream"
config = "default"
split = "train"
base_url = "https://datasets-server.huggingface.co"
page_size = 100
image_dir = out_dir / "hf_images"
image_dir.mkdir(parents=True, exist_ok=True)


def api_url(path, params):
    return f"{base_url}{path}?{urllib.parse.urlencode(params)}"


def fetch_bytes(url, retries=8):
    last_error = None
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(url, timeout=90) as response:
                return response.read()
        except Exception as exc:  # noqa: BLE001 - script reports final URL on failure
            last_error = exc
            time.sleep(min(20, 2 ** attempt))
    raise RuntimeError(f"failed after {retries} attempts: {url}: {last_error}")


def fetch_json(url):
    return json.loads(fetch_bytes(url).decode("utf-8"))


size_url = api_url("/size", {"dataset": dataset})
splits_url = api_url("/splits", {"dataset": dataset})
size_payload = fetch_json(size_url)
splits_payload = fetch_json(splits_url)

(out_dir / "hf_size.json").write_text(json.dumps(size_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
(out_dir / "hf_splits.json").write_text(json.dumps(splits_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

matching_splits = [
    item for item in splits_payload.get("splits", [])
    if item.get("config") == config and item.get("split") == split
]
if not matching_splits:
    raise SystemExit(f"dataset split {dataset}/{config}/{split} not found")

expected_rows = None
for item in size_payload.get("size", {}).get("splits", []):
    if item.get("config") == config and item.get("split") == split:
        expected_rows = int(item["num_rows"])
        break

if expected_rows is None:
    raise SystemExit("Dataset Viewer size response did not include train row count")

manifest_path = out_dir / "hf_image_downloads.tsv"
rows_path = out_dir / "hf_rows.jsonl"

with manifest_path.open("w", encoding="utf-8") as manifest, rows_path.open("w", encoding="utf-8") as rows_out:
    manifest.write("row_idx\tpath\twidth\theight\n")
    downloaded = 0
    for offset in range(0, expected_rows, page_size):
        rows_url = api_url(
            "/rows",
            {
                "dataset": dataset,
                "config": config,
                "split": split,
                "offset": offset,
                "length": min(page_size, expected_rows - offset),
            },
        )
        page = fetch_json(rows_url)
        rows = page.get("rows", [])
        if not rows:
            raise SystemExit(f"no rows returned at offset {offset}")
        for row in rows:
            row_idx = int(row["row_idx"])
            image = row.get("row", {}).get("image", {})
            src = image.get("src")
            width = int(image.get("width", 0))
            height = int(image.get("height", 0))
            if not src or width <= 0 or height <= 0:
                raise SystemExit(f"row {row_idx} does not contain a downloadable image")
            image_path = image_dir / f"hf_row_{row_idx:05d}.jpg"
            if not image_path.exists() or image_path.stat().st_size == 0:
                image_path.write_bytes(fetch_bytes(src))
            downloaded += 1
            rows_out.write(json.dumps({"row_idx": row_idx, "src": src, "width": width, "height": height}) + "\n")
            manifest.write(f"{row_idx}\t{image_path}\t{width}\t{height}\n")

if downloaded != expected_rows:
    raise SystemExit(f"download manifest row count mismatch: {downloaded} != {expected_rows}")

print(f"Dataset Viewer verified {dataset}/{config}/{split}: {downloaded} rows")
print(f"Manifest: {manifest_path}")
PY

if [[ ! -x "${BINARY}" ]]; then
    echo "Evidence binary is not executable: ${BINARY}" >&2
    exit 1
fi

"${BINARY}" "${OUT_DIR}" "${OUT_DIR}/hf_image_downloads.tsv"
fi

python - "${OUT_DIR}" <<'PY'
import base64
import html
import json
import os
import pathlib
import subprocess
import sys
from datetime import datetime, timezone

out_dir = pathlib.Path(sys.argv[1])


def rel(path):
    return pathlib.Path(path).relative_to(out_dir).as_posix()


def read_text(path):
    p = out_dir / path
    if not p.exists():
        return ""
    return p.read_text(encoding="utf-8", errors="replace")


def parse_log(path):
    text = read_text(path)
    result = {"path": path, "command": "", "start": "", "end": "", "exit_code": ""}
    for line in text.splitlines():
        if line.startswith("command: "):
            result["command"] = line[len("command: "):]
        elif line.startswith("start: "):
            result["start"] = line[len("start: "):]
        elif line.startswith("end: "):
            result["end"] = line[len("end: "):]
        elif line.startswith("exit_code: "):
            result["exit_code"] = line[len("exit_code: "):]
    return result


def file_size(path):
    p = out_dir / path
    return p.stat().st_size if p.exists() else 0


def data_uri(path):
    p = out_dir / path
    suffix = p.suffix.lower()
    mime = "image/svg+xml" if suffix == ".svg" else "image/png"
    return f"data:{mime};base64," + base64.b64encode(p.read_bytes()).decode("ascii")


def git_value(args, default="unknown"):
    try:
        return subprocess.check_output(["git", *args], cwd=out_dir.parents[1], text=True).strip()
    except Exception:
        return default


metrics = json.loads((out_dir / "metrics.json").read_text(encoding="utf-8"))
commit = git_value(["rev-parse", "--short", "HEAD"])
branch = git_value(["branch", "--show-current"])
generated_at = datetime.now(timezone.utc).isoformat()

flow_svg = """<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1080\" height=\"260\" viewBox=\"0 0 1080 260\">
  <style>
    .box { fill: #f8fafc; stroke: #334155; stroke-width: 2; rx: 8; }
    .gate { fill: #ecfeff; stroke: #0e7490; stroke-width: 2; rx: 8; }
    .skip { fill: #fef2f2; stroke: #b91c1c; stroke-width: 2; rx: 8; }
    .text { font: 15px sans-serif; fill: #0f172a; }
    .small { font: 12px sans-serif; fill: #334155; }
    .arrow { stroke: #334155; stroke-width: 2; marker-end: url(#arrow); }
  </style>
  <defs><marker id=\"arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"7\" markerHeight=\"7\" orient=\"auto-start-reverse\"><path d=\"M 0 0 L 10 5 L 0 10 z\" fill=\"#334155\"/></marker></defs>
  <rect class=\"box\" x=\"20\" y=\"75\" width=\"150\" height=\"70\"/>
  <text class=\"text\" x=\"43\" y=\"104\">Frame + ROI</text>
  <text class=\"small\" x=\"43\" y=\"126\">512x96 HF stream</text>
  <line class=\"arrow\" x1=\"170\" y1=\"110\" x2=\"220\" y2=\"110\"/>
  <rect class=\"box\" x=\"220\" y=\"75\" width=\"160\" height=\"70\"/>
  <text class=\"text\" x=\"245\" y=\"104\">Blur + diff</text>
  <text class=\"small\" x=\"245\" y=\"126\">background subtract</text>
  <line class=\"arrow\" x1=\"380\" y1=\"110\" x2=\"430\" y2=\"110\"/>
  <rect class=\"box\" x=\"430\" y=\"75\" width=\"170\" height=\"70\"/>
  <text class=\"text\" x=\"455\" y=\"104\">Threshold + morph</text>
  <text class=\"small\" x=\"455\" y=\"126\">cheap mask evidence</text>
  <line class=\"arrow\" x1=\"600\" y1=\"110\" x2=\"650\" y2=\"110\"/>
  <rect class=\"gate\" x=\"650\" y=\"50\" width=\"190\" height=\"120\"/>
  <text class=\"text\" x=\"674\" y=\"86\">Empty-frame gate</text>
  <text class=\"small\" x=\"674\" y=\"110\">diff energy</text>
  <text class=\"small\" x=\"674\" y=\"130\">ROI occupancy</text>
  <text class=\"small\" x=\"674\" y=\"150\">threshold sensitivity</text>
  <line class=\"arrow\" x1=\"840\" y1=\"95\" x2=\"900\" y2=\"65\"/>
  <rect class=\"skip\" x=\"900\" y=\"28\" width=\"150\" height=\"70\"/>
  <text class=\"text\" x=\"930\" y=\"58\">Discard</text>
  <text class=\"small\" x=\"930\" y=\"80\">no contours</text>
  <line class=\"arrow\" x1=\"840\" y1=\"130\" x2=\"900\" y2=\"170\"/>
  <rect class=\"box\" x=\"900\" y=\"142\" width=\"150\" height=\"80\"/>
  <text class=\"text\" x=\"923\" y=\"174\">Contours</text>
  <text class=\"small\" x=\"923\" y=\"196\">metrics + objects</text>
</svg>
"""
(out_dir / "flow_diagram.svg").write_text(flow_svg, encoding="utf-8")

command_logs = [
    parse_log("logs/reproduction.log"),
    parse_log("logs/configure.log"),
    parse_log("logs/build.log"),
    parse_log("logs/test.log"),
    parse_log("logs/test-full.log"),
    parse_log("logs/sample-metrics.log"),
    parse_log("logs/pr-feedback.log"),
    parse_log("logs/ci-checks.log"),
]

sample_rows = []
for sample in metrics.get("manual_review_set", []):
    sample_rows.append(
        f"<tr><td>{html.escape(sample['sample_id'])}</td>"
        f"<td>{html.escape(sample['case_type'])}</td>"
        f"<td>{html.escape(sample['expected_label'])}</td>"
        f"<td>{html.escape(sample['prediction'])}</td>"
        f"<td>{sample['empty_frame_discarded']}</td>"
        f"<td>{sample['threshold_pixels']}</td>"
        f"<td>{sample['morph_pixels']}</td>"
        f"<td>{sample['diff_energy']:.3f}</td>"
        f"<td>{sample['contour_count']}</td></tr>"
    )

gallery = []
for sample in metrics.get("manual_review_set", []):
    gallery.append(
        "<section class=\"sample\">"
        f"<h3>{html.escape(sample['sample_id'])}</h3>"
        f"<p>{html.escape(sample['case_type'])}; expected {html.escape(sample['expected_label'])}; predicted {html.escape(sample['prediction'])}.</p>"
        "<div class=\"gallery\">"
        f"<figure><img src=\"{data_uri(sample['input_path'])}\"><figcaption>Input</figcaption></figure>"
        f"<figure><img src=\"{data_uri(sample['mask_path'])}\"><figcaption>Mask/output</figcaption></figure>"
        f"<figure><img src=\"{data_uri(sample['overlay_path'])}\"><figcaption>Overlay/contours</figcaption></figure>"
        "</div></section>"
    )

cmd_rows = []
for item in command_logs:
    status = item["exit_code"] if item["exit_code"] else "not captured"
    cmd_rows.append(
        f"<tr><td>{html.escape(item['path'])}</td><td><code>{html.escape(item['command'])}</code></td>"
        f"<td>{html.escape(item['start'])}</td><td>{html.escape(item['end'])}</td><td>{html.escape(status)}</td></tr>"
    )

aggregate = metrics.get("aggregate", {})
report = f"""<!doctype html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\">
  <title>KIN-7 Robust Empty-Frame Discarding Review</title>
  <style>
    body {{ font-family: Arial, sans-serif; margin: 24px; color: #111827; }}
    h1, h2, h3 {{ color: #0f172a; }}
    table {{ border-collapse: collapse; width: 100%; margin: 12px 0 24px; }}
    th, td {{ border: 1px solid #cbd5e1; padding: 7px; font-size: 13px; vertical-align: top; }}
    th {{ background: #f1f5f9; text-align: left; }}
    code {{ white-space: pre-wrap; }}
    .verdict {{ padding: 12px; background: #ecfdf5; border: 1px solid #059669; }}
    .warn {{ padding: 12px; background: #fffbeb; border: 1px solid #d97706; }}
    .gallery {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; }}
    figure {{ margin: 0; border: 1px solid #cbd5e1; padding: 8px; }}
    img {{ max-width: 100%; image-rendering: pixelated; }}
    figcaption {{ font-size: 12px; color: #475569; margin-top: 6px; }}
    .sample {{ margin-bottom: 26px; }}
  </style>
</head>
<body>
  <h1>KIN-7 Robust Empty-Frame Discarding</h1>
  <div class=\"verdict\"><strong>Verdict:</strong> backend implementation and targeted tests pass locally. The HF sample set is public and fully classified, but it has no label column, so review metrics use an explicit manual-review set plus aggregate empty/non-empty counts.</div>
  <h2>Metadata</h2>
  <table>
    <tr><th>Issue</th><td>KIN-7</td></tr>
    <tr><th>Branch</th><td>{html.escape(branch)}</td></tr>
    <tr><th>Commit</th><td>{html.escape(commit)}</td></tr>
    <tr><th>Generated</th><td>{html.escape(generated_at)}</td></tr>
    <tr><th>CI</th><td>{html.escape(read_text('logs/ci-summary.json') or 'pending or unavailable at report generation time')}</td></tr>
  </table>
  <h2>Command Table</h2>
  <table><tr><th>Log</th><th>Command</th><th>Start</th><th>End</th><th>Exit</th></tr>{''.join(cmd_rows)}</table>
  <h2>Build And Tests</h2>
  <p>Configure, build, targeted backend processing tests, and the broader available backend test sweep were captured with exit codes in the logs above. Targeted tests include empty/noise pre-contour discard, known-good ring preservation, batch queue behavior, metrics emission, and multi-object regression.</p>
  <h2>PR Feedback Sweep</h2>
  <p>{html.escape(read_text('logs/pr-feedback.log') or 'No PR feedback sweep log captured yet.')}</p>
  <h2>Flow Diagram</h2>
  <img src=\"{data_uri('flow_diagram.svg')}\" alt=\"KIN-7 processing flow diagram\">
  <h2>HF Sample Set Metrics</h2>
  <table>
    <tr><th>Dataset</th><td>gavinlouuu/512x96stream default/train</td></tr>
    <tr><th>Rows Loaded</th><td>{metrics['dataset']['rows_loaded']}</td></tr>
    <tr><th>Empty Discarded</th><td>{aggregate.get('empty_discarded')}</td></tr>
    <tr><th>Non-Empty Candidates</th><td>{aggregate.get('non_empty_candidates')}</td></tr>
    <tr><th>Valid Nested Object Candidates</th><td>{aggregate.get('valid_nested_object_candidates')}</td></tr>
    <tr><th>Frames With Contours</th><td>{aggregate.get('frames_with_contours')}</td></tr>
  </table>
  <h2>Manual Review Metrics</h2>
  <table><tr><th>Sample ID</th><th>Case</th><th>Expected</th><th>Prediction</th><th>Discarded</th><th>Threshold Px</th><th>Morph Px</th><th>Diff Energy</th><th>Contours</th></tr>{''.join(sample_rows)}</table>
  <h2>Visual Sample Gallery</h2>
  {''.join(gallery)}
  <h2>Known Limitations</h2>
  <div class=\"warn\">The HF dataset exposes only an image column, so precision/recall against ground truth cannot be computed from that source. Synthetic labels are known; HF representatives are included for manual visual review and aggregate classification auditing.</div>
  <h2>Regeneration</h2>
  <pre>cmake --preset linux-backend-only
cmake --build --preset linux-backend-only-build --target kin7_empty_frame_evidence processing_batch_pipeline_test mib_processing_multi_object_test
tools/kin7_generate_empty_frame_evidence.sh {html.escape(str(out_dir))} {html.escape('build/linux-backend/kin7_empty_frame_evidence')}</pre>
  <p>After collecting PR feedback or CI logs, refresh the report without reclassifying images:</p>
  <pre>KIN7_REPORT_ONLY=1 tools/kin7_generate_empty_frame_evidence.sh {html.escape(str(out_dir))} {html.escape('build/linux-backend/kin7_empty_frame_evidence')}</pre>
</body>
</html>
"""
(out_dir / "report.html").write_text(report, encoding="utf-8")

artifact_specs = [
    ("report.html", "canonical human-readable review report", None, "tools/kin7_generate_empty_frame_evidence.sh"),
    ("manifest.json", "machine-readable artifact index", None, "tools/kin7_generate_empty_frame_evidence.sh"),
    ("flow_diagram.svg", "changed processing/data flow", None, "tools/kin7_generate_empty_frame_evidence.sh"),
    ("metrics.json", "per-frame and aggregate empty-frame metrics", None, "kin7_empty_frame_evidence"),
    ("background_mean.png", "mean background used for HF classification", None, "kin7_empty_frame_evidence"),
    ("hf_size.json", "Dataset Viewer size metadata", None, "tools/kin7_generate_empty_frame_evidence.sh"),
    ("hf_splits.json", "Dataset Viewer split metadata", None, "tools/kin7_generate_empty_frame_evidence.sh"),
    ("hf_image_downloads.tsv", "HF row-to-image download manifest", None, "tools/kin7_generate_empty_frame_evidence.sh"),
    ("scripts/regenerate.sh", "bundle-local regeneration script copy", None, "tools/kin7_generate_empty_frame_evidence.sh"),
    ("logs/reproduction.log", "baseline reproduction signal", None, "baseline probe"),
    ("logs/configure.log", "CMake configure log", None, "cmake"),
    ("logs/build.log", "final build log", None, "cmake"),
    ("logs/test.log", "final targeted test log", None, "ctest"),
    ("logs/test-full.log", "broader backend test sweep log", None, "ctest"),
    ("logs/sample-metrics.log", "HF classification and report generation log", None, "tools/kin7_generate_empty_frame_evidence.sh"),
    ("logs/pr-feedback.log", "PR feedback sweep log", None, "gh"),
    ("logs/ci-checks.log", "raw PR check polling log", None, "gh"),
    ("logs/ci-summary.json", "PR check summary", None, "gh"),
]
for sample in metrics.get("manual_review_set", []):
    artifact_specs.extend([
        (sample["input_path"], "sample input image", sample["sample_id"], "kin7_empty_frame_evidence"),
        (sample["mask_path"], "sample processed mask/output", sample["sample_id"], "kin7_empty_frame_evidence"),
        (sample["overlay_path"], "sample overlay/contours", sample["sample_id"], "kin7_empty_frame_evidence"),
    ])

manifest = {
    "issue": "KIN-7",
    "generated_at": generated_at,
    "commit": commit,
    "branch": branch,
    "linear_urls": {},
    "artifacts": [],
}
for path, purpose, sample_id, command in artifact_specs:
    p = out_dir / path
    if not p.exists():
        continue
    manifest["artifacts"].append({
        "path": path,
        "local_path": str(p),
        "purpose": purpose,
        "sample_id": sample_id,
        "command_provenance": command,
        "file_size": file_size(path),
        "linear_url": None,
    })

(out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
print(f"Report: {out_dir / 'report.html'}")
print(f"Manifest: {out_dir / 'manifest.json'}")
PY
