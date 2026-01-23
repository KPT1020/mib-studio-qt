#!/usr/bin/env python3
"""
Convert Ultralytics YOLO11 segmentation weights (.pt) to ONNX.

Default inputs/outputs (relative to this script):
  - Input:  yolo11n-seg.pt
  - Output: yolo11n-seg.onnx

Usage (PowerShell, from repo root):
  python -m venv .venv
  .\.venv\Scripts\python -m pip install -U pip
  .\.venv\Scripts\python -m pip install ultralytics onnx
  .\.venv\Scripts\python .\resources\models\convert_yolo11n_seg_to_onnx.py
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Export yolo11n-seg.pt to ONNX using Ultralytics.")
    p.add_argument("--pt", type=str, default="yolo11n-seg.pt", help="Input .pt path (relative to script if not absolute).")
    p.add_argument("--onnx", type=str, default="yolo11n-seg.onnx", help="Output .onnx path (relative to script if not absolute).")
    p.add_argument("--imgsz", type=int, default=640, help="Export image size (square). Default: 640")
    p.add_argument("--opset", type=int, default=17, help="ONNX opset. Default: 17")
    p.add_argument("--dynamic", action="store_true", help="Enable dynamic input shapes.")
    p.add_argument("--simplify", action="store_true", help="Try to simplify ONNX (requires onnxsim).")
    return p.parse_args()


def main() -> int:
    args = parse_args()

    script_dir = Path(__file__).resolve().parent
    pt_path = Path(args.pt)
    onnx_path = Path(args.onnx)
    if not pt_path.is_absolute():
        pt_path = (script_dir / pt_path).resolve()
    if not onnx_path.is_absolute():
        onnx_path = (script_dir / onnx_path).resolve()

    if not pt_path.exists():
        print(f"ERROR: .pt not found: {pt_path}", file=sys.stderr)
        return 2

    try:
        from ultralytics import YOLO
    except Exception as e:
        print("ERROR: Failed to import ultralytics. Install it with:", file=sys.stderr)
        print("  python -m pip install ultralytics onnx", file=sys.stderr)
        print(f"Details: {e}", file=sys.stderr)
        return 3

    print(f"Loading: {pt_path}")
    model = YOLO(str(pt_path))

    export_kwargs = {
        "format": "onnx",
        "imgsz": args.imgsz,
        "opset": args.opset,
        "dynamic": bool(args.dynamic),
        "simplify": bool(args.simplify),
    }

    print(f"Exporting to ONNX: {onnx_path}")
    # Ultralytics writes into its own output dir; we move/rename to requested path afterward.
    exported = model.export(**export_kwargs)

    # exported may be a Path-like string or a dict depending on ultralytics version.
    exported_path = None
    if isinstance(exported, (str, Path)):
        exported_path = Path(exported)
    elif isinstance(exported, dict) and "file" in exported:
        exported_path = Path(exported["file"])

    # Best-effort fallback: search next to pt file for generated .onnx
    if exported_path is None:
        candidates = sorted(pt_path.parent.glob("*.onnx"), key=lambda p: p.stat().st_mtime, reverse=True)
        exported_path = candidates[0] if candidates else None

    if exported_path is None or not exported_path.exists():
        print("ERROR: Ultralytics export did not produce an ONNX file we can find.", file=sys.stderr)
        return 4

    exported_path = exported_path.resolve()
    onnx_path.parent.mkdir(parents=True, exist_ok=True)
    if exported_path != onnx_path:
        onnx_path.write_bytes(exported_path.read_bytes())
    print(f"Done: {onnx_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

