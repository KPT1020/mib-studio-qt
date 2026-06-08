#!/usr/bin/env python3
"""Download the full HF stream and run the KIN-6 evidence binaries."""

from __future__ import annotations

import argparse
import json
import pathlib
import platform
import shutil
import subprocess
import sys
import time
from typing import List, Optional

from kin10_run_hf_dataset_test import (
    DEFAULT_CONFIG,
    DEFAULT_DATASET,
    DEFAULT_SPLIT,
    api_url,
    fetch_bytes,
    fetch_json,
    image_payload,
    resolve_row_count,
)


def default_evidence_binary() -> pathlib.Path:
    if platform.system() == "Windows":
        return pathlib.Path("build") / "Debug" / "kin6_batch_pipeline_evidence.exe"
    return pathlib.Path("build") / "linux-backend" / "kin6_batch_pipeline_evidence"


def prepare_full_manifest(
    out_dir: pathlib.Path,
    dataset: str,
    config: str,
    split: str,
    page_size: int,
    expected_rows: int,
) -> pathlib.Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "logs").mkdir(parents=True, exist_ok=True)

    size_payload = fetch_json(api_url("/size", {"dataset": dataset}))
    splits_payload = fetch_json(api_url("/splits", {"dataset": dataset}))
    row_count = resolve_row_count(dataset, config, split, splits_payload, size_payload)
    if row_count != expected_rows:
        raise RuntimeError(f"expected {expected_rows} rows, Dataset Viewer reported {row_count}")

    (out_dir / "hf_size.json").write_text(
        json.dumps(size_payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (out_dir / "hf_splits.json").write_text(
        json.dumps(splits_payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    manifest_path = out_dir / "hf_image_downloads.tsv"
    rows_path = out_dir / "hf_rows.jsonl"

    with manifest_path.open("w", encoding="utf-8") as manifest, rows_path.open(
        "w", encoding="utf-8"
    ) as rows_out:
        manifest.write("row_idx\tpath\twidth\theight\n")
        downloaded = 0
        for offset in range(0, row_count, page_size):
            page = fetch_json(
                api_url(
                    "/rows",
                    {
                        "dataset": dataset,
                        "config": config,
                        "split": split,
                        "offset": offset,
                        "length": min(page_size, row_count - offset),
                    },
                )
            )
            rows = page.get("rows", [])
            if not isinstance(rows, list) or not rows:
                raise RuntimeError(f"no rows returned at offset {offset}")

            for row in rows:
                if not isinstance(row, dict):
                    raise RuntimeError(f"Dataset Viewer returned invalid row at offset {offset}")
                row_index = int(row["row_idx"])
                image = image_payload(row)
                src = image.get("src")
                width = int(image.get("width", 0))
                height = int(image.get("height", 0))
                if not src or width <= 0 or height <= 0:
                    raise RuntimeError(f"row {row_index} does not contain a downloadable image")

                image_path = out_dir / f"hf_row_{row_index:05d}.jpg"
                if not image_path.exists() or image_path.stat().st_size == 0:
                    image_path.write_bytes(fetch_bytes(str(src), retries=8))
                downloaded += 1

                rows_out.write(
                    json.dumps(
                        {
                            "row_idx": row_index,
                            "src": src,
                            "width": width,
                            "height": height,
                        },
                        sort_keys=True,
                    )
                    + "\n"
                )
                manifest.write(f"{row_index}\t{image_path.resolve()}\t{width}\t{height}\n")

    if downloaded != expected_rows:
        raise RuntimeError(f"download manifest row count mismatch: {downloaded} != {expected_rows}")

    print(f"Dataset Viewer verified {dataset}/{config}/{split}: {downloaded} rows")
    print(f"Manifest: {manifest_path}")
    return manifest_path


def run_command(command: List[str], description: str) -> int:
    binary = pathlib.Path(command[0])
    if not binary.exists():
        print(f"{description} binary does not exist: {binary}", file=sys.stderr)
        return 2
    return subprocess.run(command, check=False).returncode


def link_or_copy(source: pathlib.Path, destination: pathlib.Path) -> None:
    if destination.exists():
        destination.unlink()
    try:
        destination.hardlink_to(source)
    except OSError:
        shutil.copy2(source, destination)


def prepare_mock_frame_dir(manifest_path: pathlib.Path, mock_frame_dir: pathlib.Path) -> None:
    if mock_frame_dir.exists():
        shutil.rmtree(mock_frame_dir)
    mock_frame_dir.mkdir(parents=True, exist_ok=True)

    with manifest_path.open("r", encoding="utf-8") as manifest:
        for line in manifest:
            if not line.strip() or line.startswith("row_idx"):
                continue
            row_index, image_path, *_ = line.rstrip("\n").split("\t")
            link_or_copy(
                pathlib.Path(image_path),
                mock_frame_dir / f"hf_row_{int(row_index):05d}.jpg",
            )


def write_readme(
    out_dir: pathlib.Path,
    binary: pathlib.Path,
    app_proof_binary: Optional[pathlib.Path],
) -> None:
    app_binary_text = str(app_proof_binary) if app_proof_binary else "<optional app proof binary>"
    readme = f"""# KIN-6 Review Evidence

Generated from Hugging Face dataset `{DEFAULT_DATASET}`, config `{DEFAULT_CONFIG}`, split `{DEFAULT_SPLIT}`.

## Regenerate

```powershell
python tools/kin6_generate_hf_evidence.py --out-dir {out_dir} --binary {binary} --app-proof-binary {app_binary_text}
```

## Files

- `hf_input_sample.png` - Hugging Face input frame submitted through the async batch queue.
- `processed_mask_sample.png` - processed mask emitted by async batch workers.
- `contour_overlay_sample.png` - contour overlay for reviewer inspection.
- `metrics.json` - 5,000-frame batch stats, capture-loop probe stats, and per-frame metrics.
- `mib_app_input_sample.png` - optional app-level proof input frame.
- `mib_app_processed_mask_sample.png` - optional app-level proof mask.
- `mib_app_contour_overlay_sample.png` - optional app-level proof overlay.
- `mib_app_capture_proof.json` - optional app runtime counters.
- `hf_size.json`, `hf_splits.json`, `hf_rows.jsonl`, `hf_image_downloads.tsv` - Dataset Viewer metadata and image manifest.
"""
    (out_dir / "README.md").write_text(readme, encoding="utf-8")


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out_dir_pos", nargs="?", help="Output directory")
    parser.add_argument("binary_pos", nargs="?", help="KIN-6 evidence binary")
    parser.add_argument("app_proof_binary_pos", nargs="?", help="Optional app proof binary")
    parser.add_argument("--out-dir", help="Output directory")
    parser.add_argument("--binary", help="KIN-6 evidence binary")
    parser.add_argument("--app-proof-binary", help="Optional app proof binary")
    parser.add_argument("--dataset", default=DEFAULT_DATASET)
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--split", default=DEFAULT_SPLIT)
    parser.add_argument("--page-size", type=int, default=100)
    parser.add_argument("--expected-rows", type=int, default=5000)
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)
    out_dir = pathlib.Path(args.out_dir or args.out_dir_pos or "review_artifacts/KIN-6").resolve()
    binary = pathlib.Path(args.binary or args.binary_pos or default_evidence_binary()).resolve()
    app_proof_raw = args.app_proof_binary or args.app_proof_binary_pos
    app_proof_binary = pathlib.Path(app_proof_raw).resolve() if app_proof_raw else None

    try:
        manifest_path = prepare_full_manifest(
            out_dir=out_dir,
            dataset=args.dataset,
            config=args.config,
            split=args.split,
            page_size=args.page_size,
            expected_rows=args.expected_rows,
        )
    except Exception as exc:  # noqa: BLE001 - command-line runner should report concise context
        print(f"failed to prepare HF evidence manifest: {exc}", file=sys.stderr)
        return 1

    result = run_command([str(binary), str(out_dir), str(manifest_path)], "Evidence")
    if result != 0:
        return result

    if app_proof_binary is not None:
        mock_frame_dir = out_dir / "mib_app_mock_frames"
        prepare_mock_frame_dir(manifest_path, mock_frame_dir)
        result = run_command(
            [
                str(app_proof_binary),
                str(mock_frame_dir),
                str(out_dir),
                str(args.expected_rows),
                str(args.expected_rows),
            ],
            "MIB app capture proof",
        )
        if result != 0:
            return result

    write_readme(out_dir, binary, app_proof_binary)
    print(f"KIN-6 evidence generated at {out_dir}")
    print(f"Generated at Unix time {int(time.time())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
