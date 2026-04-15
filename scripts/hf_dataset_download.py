#!/usr/bin/env python3
"""
Download a HuggingFace image dataset into a folder of numbered grayscale PNGs.

The resulting folder is consumed as-is by MIB Studio's MockCamera
(MIB_CAMERA_MODE=mock / MIB_MOCK_CAMERA_DIR=<folder>). Frames are written in
HF row order with a zero-padded prefix so the C++ runner preserves the
original indexing end-to-end.

Usage:
    # Full train split
    python scripts/hf_dataset_download.py \
        --dataset gavinlouuu/512x96stream \
        --split train \
        --out data/hf_frames

    # First 500 rows only (for quick iteration)
    python scripts/hf_dataset_download.py \
        --dataset gavinlouuu/512x96stream \
        --split train \
        --limit 500 \
        --out data/hf_frames

    # Private dataset
    python scripts/hf_dataset_download.py ... --token $HF_TOKEN

Dependencies: datasets, huggingface_hub, Pillow (see scripts/requirements.txt).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Iterable, Optional

try:
    from datasets import load_dataset
    from PIL import Image
except ImportError as e:
    print("ERROR: Required dependencies not installed.", file=sys.stderr)
    print("Install with: pip install -r scripts/requirements.txt", file=sys.stderr)
    print(f"Details: {e}", file=sys.stderr)
    sys.exit(1)


IMAGE_COLUMN_CANDIDATES = ("image", "img", "frame", "png", "tiff")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Download a HuggingFace image dataset into a numbered PNG folder.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--dataset", required=True,
                   help="HuggingFace dataset id (e.g. gavinlouuu/512x96stream)")
    p.add_argument("--split", default="train",
                   help="Split to download. Default: train")
    p.add_argument("--out", required=True, type=Path,
                   help="Output folder (created if missing)")
    p.add_argument("--limit", type=int, default=0,
                   help="Stop after N rows. 0 means full split. Default: 0")
    p.add_argument("--image-column", default=None,
                   help=f"Column holding the image. If omitted, auto-detect "
                        f"from {IMAGE_COLUMN_CANDIDATES}")
    p.add_argument("--token", default=None,
                   help="HuggingFace access token (or set HF_TOKEN env var)")
    p.add_argument("--revision", default=None,
                   help="Dataset revision / branch / tag")
    p.add_argument("--pad-width", type=int, default=6,
                   help="Zero-padding width for frame filenames. Default: 6")
    p.add_argument("--force", action="store_true",
                   help="Re-write frames even if output files already exist")
    p.add_argument("--no-stream", action="store_true",
                   help="Disable streaming (loads the whole split into memory). "
                        "Use only when the dataset is small and streaming fails.")
    return p.parse_args()


def pick_image_column(sample: dict, explicit: Optional[str]) -> str:
    if explicit:
        if explicit not in sample:
            raise ValueError(
                f"--image-column '{explicit}' not present in row. "
                f"Available columns: {sorted(sample.keys())}"
            )
        return explicit
    for candidate in IMAGE_COLUMN_CANDIDATES:
        if candidate in sample:
            return candidate
    # Fall back to the first column whose value looks like a PIL Image
    for k, v in sample.items():
        if hasattr(v, "save") and hasattr(v, "convert"):
            return k
    raise ValueError(
        f"Could not locate an image column. Tried {IMAGE_COLUMN_CANDIDATES}. "
        f"Available columns: {sorted(sample.keys())}. "
        f"Pass --image-column explicitly."
    )


def iterate_rows(dataset: Any, limit: int) -> Iterable[dict]:
    if limit and limit > 0:
        count = 0
        for row in dataset:
            yield row
            count += 1
            if count >= limit:
                return
    else:
        yield from dataset


def to_grayscale_image(value: Any) -> Image.Image:
    """Accept a PIL.Image, a numpy array, or bytes; return an 8-bit L image."""
    if isinstance(value, Image.Image):
        img = value
    elif isinstance(value, dict) and "bytes" in value and value["bytes"]:
        from io import BytesIO
        img = Image.open(BytesIO(value["bytes"]))
    else:
        # Numpy array fallback (datasets without the Image feature decoded)
        try:
            import numpy as np  # noqa: F401 (optional)
            img = Image.fromarray(value)
        except Exception as exc:
            raise TypeError(
                f"Unsupported image value type {type(value)!r}; expected PIL.Image, "
                f"dict with 'bytes', or ndarray."
            ) from exc
    if img.mode != "L":
        img = img.convert("L")
    return img


def main() -> int:
    args = parse_args()
    out_dir: Path = args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    load_kwargs: dict[str, Any] = {"split": args.split}
    if args.revision:
        load_kwargs["revision"] = args.revision
    if args.token:
        load_kwargs["token"] = args.token
    if not args.no_stream:
        load_kwargs["streaming"] = True

    print(f"Loading {args.dataset} [{args.split}] "
          f"(streaming={not args.no_stream})", flush=True)
    try:
        ds = load_dataset(args.dataset, **load_kwargs)
    except Exception as exc:
        print(f"ERROR: load_dataset failed: {exc}", file=sys.stderr)
        return 1

    # Peek one row to detect the image column without consuming it permanently.
    peek_iter = iter(ds)
    try:
        first = next(peek_iter)
    except StopIteration:
        print("ERROR: dataset is empty", file=sys.stderr)
        return 1

    try:
        image_col = pick_image_column(first, args.image_column)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"Image column: {image_col}")

    def all_rows() -> Iterable[dict]:
        yield first
        yield from peek_iter

    pad = max(1, args.pad_width)
    written = 0
    skipped = 0
    for idx, row in enumerate(iterate_rows(all_rows(), args.limit)):
        fname = f"frame_{idx:0{pad}d}.png"
        fpath = out_dir / fname
        if fpath.exists() and not args.force:
            skipped += 1
            continue
        try:
            img = to_grayscale_image(row[image_col])
        except Exception as exc:
            print(f"WARN: row {idx}: failed to decode image ({exc}); skipping",
                  file=sys.stderr)
            continue
        img.save(fpath, format="PNG", optimize=False)
        written += 1
        if written % 200 == 0:
            print(f"  wrote {written} frames...", flush=True)

    manifest = {
        "dataset": args.dataset,
        "split": args.split,
        "revision": args.revision,
        "image_column": image_col,
        "rows_written": written,
        "rows_skipped_existing": skipped,
        "pad_width": pad,
        "limit": args.limit,
    }
    with open(out_dir / "hf_manifest.json", "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print(f"Done. wrote={written} skipped={skipped} -> {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
