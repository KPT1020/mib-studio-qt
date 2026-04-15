#!/usr/bin/env python3
"""
Export a CNN-training-friendly dataset from an experiment HDF5 produced by
`hf_pipeline_runner` (or any MIB Studio experiment HDF5).

Layout written:

    <out>/
      images/<index>.png       # raw 8-bit grayscale original frame
      masks/<index>.png        # binary mask from ProcessingService (valid frames only)
      labels.csv               # one row per frame, full metrics + labels + provenance
      dataset_info.json        # dataset-wide metadata (source, config, timing, counts)
      background.png           # background image used (if stored)

`labels.csv` columns:
    index, source_dataset, source_split, source_row,
    image_path, mask_path,
    is_valid, is_target_group, touches_border, has_single_inner_contour,
    inner_contour_count, rejection_reason,
    deformability, area_px, area_microns, area_ratio, ring_ratio,
    brightness_q1, brightness_q2, brightness_q3, brightness_q4,
    youngs_modulus

Rejection reasons use the same taxonomy as scripts/reanalyse_hdf5.py:
    valid, no_contours, no_single_inner_contour, touches_border,
    area_out_of_range, ring_ratio_out_of_range, deformability_out_of_range.

Note: rejection reasons are DERIVED from the stored per-frame fields, not
re-computed from pixels. For pixel-level reanalysis use reanalyse_hdf5.py.

Usage:
    python scripts/hf_cnn_export.py -i experiment.h5 -o cnn_dataset

Dependencies: h5py, numpy, opencv-python (same as export_hdf5.py).
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any, Optional

try:
    import cv2
    import h5py
    import numpy as np
except ImportError as e:
    print("ERROR: Required dependencies not installed.", file=sys.stderr)
    print("Install with: pip install h5py numpy opencv-python", file=sys.stderr)
    print(f"Details: {e}", file=sys.stderr)
    sys.exit(1)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_hdf5 import read_experiment_info, read_hdf5_metadata  # noqa: E402


# Rejection reason taxonomy, mirrored from reanalyse_hdf5.py for consistency.
REASON_VALID = "valid"
REASON_NO_CONTOURS = "no_contours"
REASON_NO_SINGLE_INNER = "no_single_inner_contour"
REASON_TOUCHES_BORDER = "touches_border"
REASON_AREA_OOR = "area_out_of_range"
REASON_RING_OOR = "ring_ratio_out_of_range"
REASON_DEFORM_OOR = "deformability_out_of_range"

# Columns common to all rows (valid + invalid).
CSV_COLUMNS = [
    "index", "source_dataset", "source_split", "source_row",
    "image_path", "mask_path",
    "is_valid", "is_target_group", "touches_border", "has_single_inner_contour",
    "inner_contour_count", "rejection_reason",
    "deformability", "area_px", "area_microns", "area_ratio", "ring_ratio",
    "brightness_q1", "brightness_q2", "brightness_q3", "brightness_q4",
    "youngs_modulus",
]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Export MIB Studio experiment HDF5 to a flat CNN-training layout.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--input", "-i", required=True, type=Path,
                   help="Path to experiment .h5 file")
    p.add_argument("--output", "-o", required=True, type=Path,
                   help="Output directory (created if missing)")
    p.add_argument("--pixel-to-micron", type=float, default=None,
                   help="Override pixel-to-micron factor for area_microns. "
                        "Default: read from /experiment_info if present else 0.4886")
    p.add_argument("--source-dataset", default=None,
                   help="HF dataset id to stamp in labels.csv (e.g. gavinlouuu/512x96stream). "
                        "If omitted, read from hf_manifest.json next to input, else empty.")
    p.add_argument("--source-split", default=None,
                   help="HF split name to stamp in labels.csv")
    p.add_argument("--hf-manifest", type=Path, default=None,
                   help="Optional explicit path to hf_manifest.json "
                        "(produced by hf_dataset_download.py)")
    p.add_argument("--write-invalid-masks", action="store_true",
                   help="Also write masks/ for invalid frames (blank/empty for many). "
                        "Default: valid frames only.")
    p.add_argument("--area-ratio-max", type=float, default=None,
                   help="Override area_ratio_threshold_max for rejection-reason derivation. "
                        "Default: read from /experiment_info if present else 1.5")
    return p.parse_args()


def _attr(info: Optional[dict], key: str, default: Any) -> Any:
    if not info:
        return default
    if key not in info:
        return default
    v = info[key]
    try:
        if hasattr(v, "item"):
            v = v.item()
    except Exception:
        pass
    if isinstance(v, bytes):
        try:
            v = v.decode("utf-8")
        except Exception:
            v = v.decode("latin-1", errors="replace")
    return v


def _ensure_grayscale_2d(img: np.ndarray) -> np.ndarray:
    img = np.asarray(img)
    if img.ndim == 3 and img.shape[-1] == 1:
        img = img[..., 0]
    elif img.ndim == 3:
        img = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    if img.dtype != np.uint8:
        if img.size and img.max() > 255:
            img = (img.astype(np.float64) / float(img.max()) * 255).astype(np.uint8)
        else:
            img = img.astype(np.uint8)
    return np.ascontiguousarray(img)


def derive_rejection_reason(
    meta_row: np.void,
    config: dict[str, Any],
) -> str:
    """Map per-frame metadata to a single rejection reason.

    Precedence mirrors ProcessingService::filterProcessedImage so that a frame
    rejected by the earliest check in the real pipeline gets that same reason.
    """
    is_valid = bool(int(meta_row["isValid"]))
    if is_valid:
        return REASON_VALID

    inner_count = int(meta_row["innerContourCount"])
    has_single_inner = bool(int(meta_row["hasSingleInnerContour"]))
    touches_border = bool(int(meta_row["touchesBorder"]))
    area_px = float(meta_row["area"])
    ring_ratio = float(meta_row["ringRatio"])
    deform = float(meta_row["deformability"])
    area_ratio = float(meta_row["areaRatio"])

    # No contours: area==0 and no inner contours AND not touching a border.
    # area==0 with inner_count==0 is the clearest signal there was nothing to measure.
    if inner_count == 0 and area_px == 0.0 and not touches_border:
        return REASON_NO_CONTOURS

    if not has_single_inner and config.get("require_single_inner_contour", True):
        return REASON_NO_SINGLE_INNER

    if touches_border and config.get("enable_border_check", True):
        return REASON_TOUCHES_BORDER

    # Area range check (µm² in config, px² in stored `area`; convert).
    if config.get("enable_area_range_check", True):
        ptm = float(config.get("pixel_to_micron_factor", 0.4886))
        area_um = area_px * (ptm * ptm)
        amin = float(config.get("area_threshold_min", 60))
        amax = float(config.get("area_threshold_max", 290))
        if not (amin <= area_um <= amax):
            return REASON_AREA_OOR

    if config.get("enable_ring_ratio_check", True):
        rmin = float(config.get("ring_ratio_min", 15.0))
        rmax = float(config.get("ring_ratio_max", 25.0))
        if not (rmin < ring_ratio < rmax):
            return REASON_RING_OOR

    if config.get("enable_deformability_range_check", False):
        dmin = float(config.get("deformability_threshold_min", 0.0))
        dmax = float(config.get("deformability_threshold_max", 1.0))
        if not (dmin <= deform <= dmax):
            return REASON_DEFORM_OOR

    # Area ratio check (hull/contour circularity proxy; less common).
    if config.get("enable_area_ratio_check", False):
        armax = float(config.get("area_ratio_threshold_max", 1.5))
        if area_ratio > armax:
            return REASON_AREA_OOR  # reuse area bucket

    # Fallback: we know isValid==False but no specific signal fired; usually area==0
    # after morphology erased the contour.
    if area_px == 0.0:
        return REASON_NO_CONTOURS
    return REASON_AREA_OOR


def build_config_from_exp_info(
    exp_info: Optional[dict],
    args: argparse.Namespace,
) -> dict[str, Any]:
    """Build the derivation config from /experiment_info attrs + CLI overrides.

    writeExperimentInfo serialises ProcessingConfig flat-prefixed
    (e.g. `cfg_area_threshold_min`). If those are missing, fall back to defaults.
    """
    def g(k: str, default: Any) -> Any:
        if exp_info is None:
            return default
        # Common prefix patterns used by Hdf5Service.writeExperimentInfo
        for candidate in (k, f"cfg_{k}", f"processing_{k}", f"config_{k}"):
            if candidate in exp_info:
                v = exp_info[candidate]
                try:
                    if hasattr(v, "item"):
                        v = v.item()
                except Exception:
                    pass
                return v
        return default

    ptm = args.pixel_to_micron
    if ptm is None:
        ptm = float(g("pixel_to_micron_factor", 0.4886))

    area_ratio_max = args.area_ratio_max
    if area_ratio_max is None:
        area_ratio_max = float(g("area_ratio_threshold_max", 1.5))

    return {
        "pixel_to_micron_factor": ptm,
        "area_threshold_min": float(g("area_threshold_min", 60)),
        "area_threshold_max": float(g("area_threshold_max", 290)),
        "deformability_threshold_min": float(g("deformability_threshold_min", 0.0)),
        "deformability_threshold_max": float(g("deformability_threshold_max", 1.0)),
        "area_ratio_threshold_max": area_ratio_max,
        "ring_ratio_min": float(g("ring_ratio_min", 15.0)),
        "ring_ratio_max": float(g("ring_ratio_max", 25.0)),
        "enable_border_check": bool(g("enable_border_check", True)),
        "enable_area_range_check": bool(g("enable_area_range_check", True)),
        "enable_ring_ratio_check": bool(g("enable_ring_ratio_check", True)),
        "enable_deformability_range_check": bool(g("enable_deformability_range_check", False)),
        "enable_area_ratio_check": bool(g("enable_area_ratio_check", False)),
        "require_single_inner_contour": bool(g("require_single_inner_contour", True)),
    }


def resolve_hf_provenance(args: argparse.Namespace) -> tuple[str, str]:
    if args.source_dataset or args.source_split:
        return (args.source_dataset or "", args.source_split or "")
    manifest_path = args.hf_manifest
    if manifest_path is None:
        # Look next to input .h5 (common case: hf_frames/ sibling)
        candidates = [
            args.input.parent / "hf_manifest.json",
            args.input.parent.parent / "hf_frames" / "hf_manifest.json",
        ]
        for c in candidates:
            if c.exists():
                manifest_path = c
                break
    if manifest_path and manifest_path.exists():
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                m = json.load(f)
            return (m.get("dataset", "") or "", m.get("split", "") or "")
        except Exception as exc:
            print(f"WARN: failed to read {manifest_path}: {exc}", file=sys.stderr)
    return ("", "")


def _row_to_csv(
    csv_index: int,
    frame_index: int,
    source_dataset: str,
    source_split: str,
    source_row: int,
    image_name: str,
    mask_name: str,
    meta_row: np.void,
    reason: str,
    pixel_to_micron: float,
    is_target_group: bool,
) -> dict[str, Any]:
    area_px = float(meta_row["area"])
    return {
        "index": csv_index,
        "source_dataset": source_dataset,
        "source_split": source_split,
        "source_row": source_row,
        "image_path": image_name,
        "mask_path": mask_name,
        "is_valid": int(meta_row["isValid"]),
        "is_target_group": int(is_target_group),
        "touches_border": int(meta_row["touchesBorder"]),
        "has_single_inner_contour": int(meta_row["hasSingleInnerContour"]),
        "inner_contour_count": int(meta_row["innerContourCount"]),
        "rejection_reason": reason,
        "deformability": f"{float(meta_row['deformability']):.6f}",
        "area_px": f"{area_px:.4f}",
        "area_microns": f"{area_px * pixel_to_micron * pixel_to_micron:.4f}",
        "area_ratio": f"{float(meta_row['areaRatio']):.6f}",
        "ring_ratio": f"{float(meta_row['ringRatio']):.6f}",
        "brightness_q1": f"{float(meta_row['brightness_q1']):.4f}",
        "brightness_q2": f"{float(meta_row['brightness_q2']):.4f}",
        "brightness_q3": f"{float(meta_row['brightness_q3']):.4f}",
        "brightness_q4": f"{float(meta_row['brightness_q4']):.4f}",
        "youngs_modulus": (
            f"{float(meta_row['youngsModulus']):.6f}"
            if "youngsModulus" in meta_row.dtype.names else ""
        ),
        "frame_index": frame_index,  # informational duplicate of index; not in header but kept for debug
    }


def export(args: argparse.Namespace) -> int:
    if not args.input.is_file():
        print(f"ERROR: input not found: {args.input}", file=sys.stderr)
        return 1
    args.output.mkdir(parents=True, exist_ok=True)
    images_dir = args.output / "images"
    masks_dir = args.output / "masks"
    images_dir.mkdir(exist_ok=True)
    masks_dir.mkdir(exist_ok=True)

    source_dataset, source_split = resolve_hf_provenance(args)

    with h5py.File(args.input, "r") as h5:
        exp_info = read_experiment_info(h5)
        config = build_config_from_exp_info(exp_info, args)
        pixel_to_micron = float(config["pixel_to_micron_factor"])

        rows: list[dict[str, Any]] = []
        total_valid = 0
        total_invalid = 0

        # Global enumeration to keep filenames dense (000000.png, 000001.png, ...).
        # Valid frames first, then invalid — keeps related rows together for CSV scans.
        csv_idx = 0

        def _process_group(
            group_prefix: str,
            is_valid_group: bool,
        ) -> None:
            nonlocal csv_idx, total_valid, total_invalid
            images_ds = f"{group_prefix}/images"
            meta_ds = f"{group_prefix}/metadata"
            if images_ds not in h5 or meta_ds not in h5:
                return
            images = h5[images_ds]
            meta = read_hdf5_metadata(h5, meta_ds)
            if meta is None or len(meta) == 0:
                return
            has_masks = f"{group_prefix}/masks" in h5
            masks = h5[f"{group_prefix}/masks"] if has_masks else None

            for i in range(len(meta)):
                m = meta[i]
                frame_index = int(m["index"])
                reason = derive_rejection_reason(m, config)
                is_target_group = False
                if "isTargetGroup" in m.dtype.names:
                    is_target_group = bool(int(m["isTargetGroup"]))

                image_name = f"{csv_idx:06d}.png"
                img = _ensure_grayscale_2d(np.asarray(images[i]))
                cv2.imwrite(str(images_dir / image_name), img)

                mask_name = ""
                write_mask = has_masks and (is_valid_group or args.write_invalid_masks)
                if write_mask:
                    mask = _ensure_grayscale_2d(np.asarray(masks[i]))
                    # Binarize: ProcessingService masks are already 0/255, but normalize anyway.
                    mask = np.where(mask > 0, 255, 0).astype(np.uint8)
                    mask_name = f"{csv_idx:06d}.png"
                    cv2.imwrite(str(masks_dir / mask_name), mask)

                rows.append(_row_to_csv(
                    csv_idx,
                    frame_index,
                    source_dataset,
                    source_split,
                    frame_index,  # source_row: downloader writes in HF row order
                    f"images/{image_name}",
                    f"masks/{mask_name}" if mask_name else "",
                    m,
                    reason,
                    pixel_to_micron,
                    is_target_group,
                ))
                csv_idx += 1
                if is_valid_group:
                    total_valid += 1
                else:
                    total_invalid += 1

        _process_group("/valid_frames", is_valid_group=True)
        _process_group("/invalid_frames", is_valid_group=False)

        # Background image, if stored.
        bg_path = ""
        if "/experiment_info/background" in h5:
            bg_raw = np.asarray(h5["/experiment_info/background"])
            if bg_raw.size > 0:
                if bg_raw.ndim == 3 and bg_raw.shape[0] == 1:
                    bg_raw = np.squeeze(bg_raw, 0)
                bg_img = _ensure_grayscale_2d(bg_raw)
                bg_path = "background.png"
                cv2.imwrite(str(args.output / bg_path), bg_img)

    # Write labels.csv.
    csv_path = args.output / "labels.csv"
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        for r in rows:
            writer.writerow({k: r.get(k, "") for k in CSV_COLUMNS})

    # Write dataset_info.json.
    info: dict[str, Any] = {
        "source_dataset": source_dataset,
        "source_split": source_split,
        "input_h5": str(args.input),
        "frame_count": len(rows),
        "valid_count": total_valid,
        "invalid_count": total_invalid,
        "pixel_to_micron_factor": pixel_to_micron,
        "processing_config": config,
        "background_path": bg_path,
    }
    if exp_info:
        for k in ("start_time_ns", "end_time_ns",
                  "total_valid_frames", "total_invalid_frames",
                  "roi_x", "roi_y", "roi_w", "roi_h"):
            v = _attr(exp_info, k, None)
            if v is not None:
                info[k] = v
    with open(args.output / "dataset_info.json", "w", encoding="utf-8") as f:
        json.dump(info, f, indent=2, default=str)

    print(f"hf_cnn_export: wrote {len(rows)} frames "
          f"(valid={total_valid}, invalid={total_invalid}) -> {args.output}")
    return 0


def main() -> int:
    args = parse_args()
    try:
        return export(args)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
