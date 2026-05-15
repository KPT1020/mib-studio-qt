#!/usr/bin/env python3
"""
Reanalyse MIB Studio HDF5 dataset: re-run the processing pipeline and save
all intermediate images so they can be subsequently analysed.

Pipeline (matches ProcessingService): grayscale -> Gaussian blur -> (optional)
background subtract -> binary threshold -> morphology (close, open) -> mask.
Optionally recomputes metrics (contours, deformability, area, ring ratio) and
exports CSV and/or a new HDF5 with recomputed masks.

Usage:
    # Reanalyse and save all intermediates (background = stored in .h5, or from_all if absent)
    python scripts/reanalyse_hdf5.py -i experiment.h5 -o ./reanalysis

    # Reanalyse every HDF5 file under a folder (experiment or recording-mode files)
    python scripts/reanalyse_hdf5.py -i ./data -o ./reanalysis

    # No background subtraction, save contour overlay and CSV
    python scripts/reanalyse_hdf5.py -i experiment.h5 -o ./reanalysis --background none --save-overlay --export-csv

    # Large recording folder: recompute metrics only, without per-frame TIFFs
    python scripts/reanalyse_hdf5.py -i ./data -o ./reanalysis --no-save-intermediates

    # Export a new HDF5 with recomputed masks for MIB Studio
    python scripts/reanalyse_hdf5.py -i experiment.h5 -o ./reanalysis --export-h5

Dependencies: h5py, numpy, opencv-python (same as export_hdf5.py).
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Optional

# Allow importing from same directory when run from repo root
sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_hdf5 import read_experiment_info, read_hdf5_images, read_hdf5_metadata

try:
    import cv2
    import h5py
    import numpy as np
except ImportError as e:
    print("ERROR: Required dependencies not installed.", file=sys.stderr)
    print("Install with: pip install h5py numpy opencv-python", file=sys.stderr)
    print(f"Details: {e}", file=sys.stderr)
    sys.exit(1)

# Default processing parameters (match config.json / ProcessingService)
DEFAULT_BLUR = 3
DEFAULT_THRESHOLD = 8
DEFAULT_MORPH_KERNEL = 3
DEFAULT_MORPH_ITERATIONS = 1
MIN_NOISE_AREA = 10.0
BORDER_THRESHOLD = 2
RING_RATIO_MIN = 15.0
RING_RATIO_MAX = 25.0

# Percentile range for synthetic ROI from object centroids (5th--95th)
SYNTHETIC_ROI_PERCENTILE_LO = 5.0
SYNTHETIC_ROI_PERCENTILE_HI = 95.0
SYNTHETIC_ROI_MARGIN_FRAC = 0.02  # 2% margin on each side
SYNTHETIC_ROI_MAX_SAMPLE_FRAMES = 500  # Cap frames when estimating ROI from masks

# Stable filter trace / rejection reason strings (used in CSV + overlay text)
REASON_VALID = "valid"
REASON_NO_CONTOURS = "no_contours"
REASON_NO_SINGLE_INNER = "no_single_inner_contour"
REASON_TOUCHES_BORDER = "touches_border"
REASON_AREA_OOR = "area_out_of_range"
REASON_RING_OOR = "ring_ratio_out_of_range"
REASON_DEFORM_OOR = "deformability_out_of_range"

FRAME_PATHS = {
    "valid": "/valid_frames",
    "invalid": "/invalid_frames",
    "recorded": "/recorded_frames",
}


def _int_attr(value: Any) -> Optional[int]:
    """Convert an HDF5 attr (possibly numpy scalar) to int, or None if invalid."""
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def extract_roi_from_exp_info(
    exp_info: Optional[dict],
    img_width: int,
    img_height: int,
) -> Optional[tuple[int, int, int, int]]:
    """
    Extract and sanitize ROI from experiment_info (roi_x, roi_y, roi_w, roi_h).
    Returns (x, y, w, h) clamped to image bounds with w, h >= 1, or None if missing/invalid.
    """
    if not exp_info or img_width <= 0 or img_height <= 0:
        return None
    x = _int_attr(exp_info.get("roi_x"))
    y = _int_attr(exp_info.get("roi_y"))
    w = _int_attr(exp_info.get("roi_w"))
    h = _int_attr(exp_info.get("roi_h"))
    if x is None or y is None or w is None or h is None or w < 1 or h < 1:
        return None
    # Clamp to image bounds
    x = max(0, min(x, img_width - 1))
    y = max(0, min(y, img_height - 1))
    w = max(1, min(w, img_width - x))
    h = max(1, min(h, img_height - y))
    return (x, y, w, h)


def get_image_shape_from_h5(h5_file: h5py.File) -> Optional[tuple[int, int]]:
    """Return (height, width) from first available images dataset, or None."""
    for path_prefix in ("/valid_frames", "/invalid_frames", "/recorded_frames"):
        images_ds = path_prefix + "/images"
        if images_ds not in h5_file:
            continue
        imgs = h5_file[images_ds]
        if imgs.shape[0] == 0:
            continue
        # shape is (n, height, width) or (n, height, width, channels)
        if imgs.ndim >= 3:
            return (int(imgs.shape[1]), int(imgs.shape[2]))
    return None


def read_group_attrs(h5_file: h5py.File, group_path: str) -> Optional[dict]:
    """Read all attributes from a group, or None if the group is absent/empty."""
    if group_path not in h5_file:
        return None
    group = h5_file[group_path]
    attrs = {name: group.attrs[name] for name in group.attrs}
    return attrs if attrs else None


def available_frame_types(h5_file: h5py.File) -> list[str]:
    """Return frame sets with images and metadata in processing order."""
    found: list[str] = []
    for frame_type, path_prefix in FRAME_PATHS.items():
        if path_prefix + "/images" in h5_file and path_prefix + "/metadata" in h5_file:
            found.append(frame_type)
    return found


def selected_frame_types(h5_file: h5py.File, requested: str) -> list[str]:
    """Map CLI frame selection to available HDF5 frame groups."""
    available = available_frame_types(h5_file)
    if requested == "all":
        return available
    if requested == "both":
        selected = [ft for ft in ("valid", "invalid") if ft in available]
        if not selected and "recorded" in available:
            selected = ["recorded"]
        return selected
    return [requested] if requested in available else []


def compute_synthetic_roi_from_valid_masks(
    h5_file: h5py.File,
    img_width: int,
    img_height: int,
) -> tuple[int, int, int, int]:
    """
    Compute synthetic ROI from valid_frames/masks: centroid percentiles (5th--95th) + margin.
    Uses only valid frames. Returns (x, y, w, h) clamped to image; if no objects, returns full frame.
    """
    masks_ds = "/valid_frames/masks"
    if masks_ds not in h5_file:
        return (0, 0, img_width, img_height)
    masks = h5_file[masks_ds]
    n = masks.shape[0]
    if n == 0:
        return (0, 0, img_width, img_height)
    sample_step = max(1, n // min(n, SYNTHETIC_ROI_MAX_SAMPLE_FRAMES))
    centroids_x: list[float] = []
    centroids_y: list[float] = []
    for idx in range(0, n, sample_step):
        mask = np.asarray(masks[idx])
        if mask.ndim > 2:
            mask = mask.squeeze()
        contours, _ = cv2.findContours(
            mask.astype(np.uint8), cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE
        )
        for c in contours:
            if cv2.contourArea(c) < MIN_NOISE_AREA:
                continue
            M = cv2.moments(c)
            if M["m00"] and M["m00"] > 0:
                cx = M["m10"] / M["m00"]
                cy = M["m01"] / M["m00"]
                centroids_x.append(cx)
                centroids_y.append(cy)
    if not centroids_x or not centroids_y:
        return (0, 0, img_width, img_height)
    x_lo = float(np.percentile(centroids_x, SYNTHETIC_ROI_PERCENTILE_LO))
    x_hi = float(np.percentile(centroids_x, SYNTHETIC_ROI_PERCENTILE_HI))
    y_lo = float(np.percentile(centroids_y, SYNTHETIC_ROI_PERCENTILE_LO))
    y_hi = float(np.percentile(centroids_y, SYNTHETIC_ROI_PERCENTILE_HI))
    margin_x = max(1, int(img_width * SYNTHETIC_ROI_MARGIN_FRAC))
    margin_y = max(1, int(img_height * SYNTHETIC_ROI_MARGIN_FRAC))
    x = max(0, int(x_lo) - margin_x)
    y = max(0, int(y_lo) - margin_y)
    x2 = min(img_width, int(x_hi) + margin_x)
    y2 = min(img_height, int(y_hi) + margin_y)
    w = max(1, x2 - x)
    h = max(1, y2 - y)
    return (x, y, w, h)


def _to_odd(v: int) -> int:
    if v < 1:
        v = 1
    if (v % 2) == 0:
        v += 1
    return v


def _ensure_grayscale(img: np.ndarray) -> np.ndarray:
    if len(img.shape) == 2:
        return np.ascontiguousarray(img)
    if img.shape[2] == 1:
        return np.ascontiguousarray(img[:, :, 0])
    return cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)


def _normalize_for_tiff(img: np.ndarray) -> np.ndarray:
    if img.dtype != np.uint8:
        if img.size == 0:
            return img.astype(np.uint8)
        mx = img.max()
        if mx > 255:
            img = (img.astype(np.float64) / mx * 255).astype(np.uint8)
        else:
            img = img.astype(np.uint8)
    return img


def _save_tiff(path: Path, img: np.ndarray) -> bool:
    img = _normalize_for_tiff(np.asarray(img))
    if len(img.shape) == 3 and img.shape[2] == 1:
        img = img[:, :, 0]
    return cv2.imwrite(str(path), img)


def build_background_from_all_images(
    h5_file: h5py.File,
    path_prefix: str,
    config: dict[str, Any],
    blur_k: int,
) -> Optional[np.ndarray]:
    """
    Build background as pixel-wise mean of all images in the dataset (one frame at a time).
    Returns blurred mean image (uint8) or None if dataset missing/empty.
    """
    images_ds_path = path_prefix + "/images"
    if images_ds_path not in h5_file:
        return None
    imgs = h5_file[images_ds_path]
    n = imgs.shape[0]
    if n == 0:
        return None
    # First frame to get shape
    first = _ensure_grayscale(np.asarray(imgs[0]))
    if first.dtype != np.uint8 and first.size > 0:
        first = (first.astype(np.float64) / first.max() * 255).astype(np.uint8)
    h, w = first.shape[:2]
    acc = np.zeros((h, w), dtype=np.float64)
    first_float = first.astype(np.float64)
    acc += first_float
    for i in range(1, n):
        frame = _ensure_grayscale(np.asarray(imgs[i]))
        if frame.ndim > 2:
            frame = np.squeeze(frame, axis=2) if frame.shape[2] == 1 else frame[:, :, 0]
        if frame.dtype != np.uint8 and frame.size > 0:
            frame = (frame.astype(np.float64) / frame.max() * 255).astype(np.uint8)
        if frame.shape[0] != h or frame.shape[1] != w:
            continue
        acc += frame.astype(np.float64)
    mean_img = np.clip(acc / n, 0, 255).astype(np.uint8)
    return cv2.GaussianBlur(mean_img, (blur_k, blur_k), 0)


def _interactive_prompt_args() -> None:
    """When running as frozen exe with no args, prompt for input and output and set sys.argv."""
    print("MIB Studio Reanalyse HDF5")
    print("Re-run the processing pipeline on an .h5 file and save intermediate images.")
    print("(Leave a line blank to exit.)")
    print()
    in_path = input("Input .h5 file: ").strip()
    if not in_path:
        sys.exit(0)
    out_path = input("Output directory: ").strip()
    if not out_path:
        sys.exit(0)
    sys.argv.extend(["-i", in_path, "-o", out_path])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Reanalyse HDF5 dataset and save all intermediate images.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--input", "-i", type=str, required=True, help="Path to input .h5/.hdf5 file, or a directory containing HDF5 files")
    parser.add_argument("--output", "-o", type=str, required=True, help="Output directory")
    parser.add_argument(
        "--frame-type", "-t",
        type=str,
        choices=["valid", "invalid", "recorded", "both", "all"],
        default="both",
        help="Process valid, invalid, recorded, both experiment frame types, or all available frame types. Default: both; recording-only files process recorded frames.",
    )
    parser.add_argument(
        "--background",
        type=str,
        default="stored",
        help="Background for subtraction: none, stored, from_all, or path to image file. Default: stored. Use 'stored' to use the background saved in the .h5 (if present; else falls back to from_all); 'from_all' to build from pixel-wise mean of all images.",
    )
    parser.add_argument(
        "--config",
        type=str,
        default=None,
        help="Path to JSON config with image_processing section (blur, threshold, morph). Overridden by CLI.",
    )
    parser.add_argument("--blur", type=int, default=None, help="Gaussian blur kernel size (odd). Overrides config.")
    parser.add_argument("--threshold", type=int, default=None, help="Binary threshold value. Overrides config.")
    parser.add_argument("--morph-kernel", type=int, default=None, help="Morphology kernel size (odd). Overrides config.")
    parser.add_argument("--morph-iterations", type=int, default=None, help="Morphology iterations. Overrides config.")
    parser.add_argument("--save-intermediates", action="store_true", dest="save_intermediates", default=True, help="Save original, blurred, diff, threshold, and mask TIFFs per frame (default).")
    parser.add_argument("--no-save-intermediates", action="store_false", dest="save_intermediates", help="Do not write per-frame intermediate TIFFs.")
    parser.add_argument(
        "--metrics-mode",
        type=str,
        choices=["auto", "frame", "objects"],
        default="auto",
        help="CSV metrics granularity. auto writes one row per detected object for recording files, otherwise one row per frame.",
    )
    parser.add_argument("--save-overlay", action="store_true", dest="save_overlay", default=True, help="Save contour overlay per frame (default).")
    parser.add_argument("--no-save-overlay", action="store_false", dest="save_overlay", help="Do not save overlay.")
    parser.add_argument("--export-csv", action="store_true", dest="export_csv", default=True, help="Recompute metrics and write metrics.csv (default).")
    parser.add_argument("--no-export-csv", action="store_false", dest="export_csv", help="Do not write metrics.csv.")
    parser.add_argument("--export-h5", action="store_true", help="Write reanalysis.h5 with images, masks, metadata.")
    parser.add_argument(
        "--pixel-to-micron",
        type=float,
        default=0.4886,
        help="Pixel to micron conversion for CSV area. Default: 0.4886",
    )
    return parser.parse_args()


def load_processing_config(args: argparse.Namespace) -> dict[str, Any]:
    cfg = {
        "gaussian_blur_size": DEFAULT_BLUR,
        "bg_subtract_threshold": DEFAULT_THRESHOLD,
        "morph_kernel_size": DEFAULT_MORPH_KERNEL,
        "morph_iterations": DEFAULT_MORPH_ITERATIONS,
        "area_threshold_min": 250,
        "area_threshold_max": 1200,
        "enable_border_check": True,
        "enable_area_range_check": True,
        "require_single_inner_contour": True,
        "deformability_threshold_min": 0.0,
        "deformability_threshold_max": 1.0,
        "enable_deformability_range_check": False,
    }
    if args.config:
        path = Path(args.config)
        if path.exists():
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            ip = data.get("image_processing", data)
            filters = ip.get("filters", {})
            cfg["gaussian_blur_size"] = ip.get("gaussian_blur_size", cfg["gaussian_blur_size"])
            cfg["bg_subtract_threshold"] = ip.get("bg_subtract_threshold", cfg["bg_subtract_threshold"])
            cfg["morph_kernel_size"] = ip.get("morph_kernel_size", cfg["morph_kernel_size"])
            cfg["morph_iterations"] = ip.get("morph_iterations", cfg["morph_iterations"])
            cfg["area_threshold_min"] = ip.get("area_threshold_min", cfg["area_threshold_min"])
            cfg["area_threshold_max"] = ip.get("area_threshold_max", cfg["area_threshold_max"])
            cfg["enable_border_check"] = filters.get("enable_border_check", cfg["enable_border_check"])
            cfg["enable_area_range_check"] = filters.get("enable_area_range_check", cfg["enable_area_range_check"])
            cfg["require_single_inner_contour"] = filters.get("require_single_inner_contour", cfg["require_single_inner_contour"])
            cfg["deformability_threshold_min"] = ip.get("deformability_threshold_min", cfg["deformability_threshold_min"])
            cfg["deformability_threshold_max"] = ip.get("deformability_threshold_max", cfg["deformability_threshold_max"])
            cfg["enable_deformability_range_check"] = filters.get("enable_deformability_range_check", cfg["enable_deformability_range_check"])
    if args.blur is not None:
        cfg["gaussian_blur_size"] = args.blur
    if args.threshold is not None:
        cfg["bg_subtract_threshold"] = args.threshold
    if args.morph_kernel is not None:
        cfg["morph_kernel_size"] = args.morph_kernel
    if args.morph_iterations is not None:
        cfg["morph_iterations"] = args.morph_iterations
    return cfg


def run_pipeline(
    gray: np.ndarray,
    bg_blurred: Optional[np.ndarray],
    config: dict[str, Any],
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Returns (blurred, diff_or_blurred, thresh, mask)."""
    blur_k = _to_odd(config["gaussian_blur_size"])
    morph_k = _to_odd(config["morph_kernel_size"])
    morph_iter = max(1, config["morph_iterations"])
    thresh_val = max(0, config["bg_subtract_threshold"])

    blurred = cv2.GaussianBlur(gray, (blur_k, blur_k), 0)
    if bg_blurred is not None and bg_blurred.shape == blurred.shape:
        diff = cv2.subtract(blurred, bg_blurred)
    else:
        diff = blurred.copy()

    _, thresh = cv2.threshold(diff, thresh_val, 255, cv2.THRESH_BINARY)
    kernel = cv2.getStructuringElement(cv2.MORPH_CROSS, (morph_k, morph_k))
    mask = cv2.morphologyEx(thresh, cv2.MORPH_CLOSE, kernel, iterations=morph_iter)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=morph_iter)
    return blurred, diff, thresh, mask


def find_contours_filtered(mask: np.ndarray) -> tuple[list, list, list, list, Any, list[int]]:
    """Mirror ProcessingService::findContours.

    Returns (filtered_contours, inner_contours, parent_indices, all_contours, hierarchy, inner_filtered_indices).
    - parent_indices are indices into filtered_contours (or -1 if parent not found)
    - inner_filtered_indices are indices into filtered_contours for each entry in inner_contours
    """
    contours, hierarchy = cv2.findContours(mask, cv2.RETR_TREE, cv2.CHAIN_APPROX_SIMPLE)[-2:]
    if hierarchy is None:
        hierarchy = np.zeros((0, 4), dtype=np.int32)
    else:
        hierarchy = np.squeeze(hierarchy, 0) if hierarchy.ndim == 3 else hierarchy

    filtered = []
    original_indices = []
    for i, c in enumerate(contours):
        if cv2.contourArea(c) >= MIN_NOISE_AREA:
            filtered.append(c)
            original_indices.append(i)

    inner_contours = []
    parent_indices = []
    inner_filtered_indices: list[int] = []
    for i, orig_i in enumerate(original_indices):
        if orig_i >= len(hierarchy):
            continue
        h = hierarchy[orig_i]
        if h[3] > -1:  # parent (original contour index)
            inner_contours.append(filtered[i])
            inner_filtered_indices.append(i)
            parent_orig = int(h[3])
            filt_parent = -1
            for j in range(len(filtered)):
                if original_indices[j] == parent_orig:
                    filt_parent = j
                    break
            parent_indices.append(filt_parent)

    return filtered, inner_contours, parent_indices, contours, hierarchy, inner_filtered_indices


def calculate_ring_ratio(inner: np.ndarray, outer: np.ndarray) -> float:
    inner_area = cv2.contourArea(inner)
    outer_area = cv2.contourArea(outer)
    if outer_area <= 0:
        return 0.0
    return math.sqrt(outer_area - inner_area)


def brightness_quantiles(gray: np.ndarray, mask: np.ndarray) -> tuple[float, float, float, float]:
    pts = np.where(mask > 0)
    if len(pts[0]) == 0:
        return 0.0, 0.0, 0.0, 0.0
    vals = np.sort(gray[pts[0], pts[1]])
    n = len(vals)
    q1 = float(vals[n // 4]) if n else 0.0
    q2 = float(vals[n // 2]) if n else 0.0
    q3 = float(vals[(3 * n) // 4]) if n else 0.0
    q4 = float(vals[-1]) if n else 0.0
    return q1, q2, q3, q4


def filter_processed_image(
    mask: np.ndarray,
    roi: tuple[int, int, int, int],
    config: dict[str, Any],
    original_gray: np.ndarray,
) -> dict[str, Any]:
    """Mirror ProcessingService::filterProcessedImage. roi = (x, y, w, h). Returns dict of metrics."""
    rx, ry, rw, rh = roi
    filtered, inner_contours, parent_indices, all_contours, hierarchy, inner_filtered_indices = find_contours_filtered(mask)

    result = {
        "isValid": False,
        "touchesBorder": False,
        "hasSingleInnerContour": len(inner_contours) == 1,
        "inRange": False,
        "innerContourCount": len(inner_contours),
        "deformability": 0.0,
        "area": 0.0,
        "areaRatio": 0.0,
        "ringRatio": 0.0,
        "brightness_q1": 0.0,
        "brightness_q2": 0.0,
        "brightness_q3": 0.0,
        "brightness_q4": 0.0,
        # Filter trace / debug fields (for reanalysis visibility)
        "rejectReason": "",
        "failedAt": "",
        "passSingleInnerCheck": False,
        "passBorderCheck": False,
        "passAreaCheck": False,
        "passRingCheck": False,
        "contourUsed": "none",  # inner | largest_outer | none
        "allContourCount": int(len(all_contours)) if all_contours is not None else 0,
        "filteredContourCount": int(len(filtered)),
        "selectedFilteredIndex": -1,
        "selectedParentFilteredIndex": -1,
        "selectedContourArea": 0.0,
        "selectedHullArea": 0.0,
    }
    if not original_gray.size:
        pass
    else:
        result["brightness_q1"], result["brightness_q2"], result["brightness_q3"], result["brightness_q4"] = brightness_quantiles(original_gray, mask)

    if len(filtered) == 0:
        result["rejectReason"] = REASON_NO_CONTOURS
        result["failedAt"] = REASON_NO_CONTOURS
        # Checks: we did not pass any contour-dependent checks
        result["passSingleInnerCheck"] = not config["require_single_inner_contour"]
        result["passBorderCheck"] = not config["enable_border_check"]
        result["passAreaCheck"] = not config["enable_area_range_check"]
        result["passRingCheck"] = False
        return result

    result["passSingleInnerCheck"] = (not config["require_single_inner_contour"]) or (len(inner_contours) >= 1)
    if config["require_single_inner_contour"] and len(inner_contours) == 0:
        result["rejectReason"] = REASON_NO_SINGLE_INNER
        result["failedAt"] = REASON_NO_SINGLE_INNER
        # Border/area/ring checks not applicable (we early return in the live pipeline)
        result["passBorderCheck"] = not config["enable_border_check"]
        result["passAreaCheck"] = not config["enable_area_range_check"]
        result["passRingCheck"] = False
        return result

    # Pre-select which contour would be used for metrics (so overlays/CSV can show it even if later rejected)
    if inner_contours:
        result["contourUsed"] = "inner"
        if inner_filtered_indices:
            result["selectedFilteredIndex"] = int(inner_filtered_indices[0])
        if parent_indices:
            result["selectedParentFilteredIndex"] = int(parent_indices[0]) if parent_indices[0] is not None else -1
    elif filtered and not config["require_single_inner_contour"]:
        idx = max(range(len(filtered)), key=lambda i: cv2.contourArea(filtered[i]))
        result["contourUsed"] = "largest_outer"
        result["selectedFilteredIndex"] = int(idx)

    # Border check
    if config["enable_border_check"]:
        check_contours = inner_contours if inner_contours else filtered
        for c in check_contours:
            for pt in c[:, 0, :]:
                x, y = int(pt[0]) - rx, int(pt[1]) - ry
                if 0 <= x < rw and 0 <= y < rh:
                    if x < BORDER_THRESHOLD or x >= rw - BORDER_THRESHOLD or y < BORDER_THRESHOLD or y >= rh - BORDER_THRESHOLD:
                        result["touchesBorder"] = True
                        break
                else:
                    result["touchesBorder"] = True
                    break
            if result["touchesBorder"]:
                break

    if result["touchesBorder"] and config["enable_border_check"]:
        result["passBorderCheck"] = False
        result["rejectReason"] = REASON_TOUCHES_BORDER
        result["failedAt"] = REASON_TOUCHES_BORDER
        return result

    result["passBorderCheck"] = True

    if inner_contours:
        c = inner_contours[0]
        contour_area = cv2.contourArea(c)
        hull = cv2.convexHull(c)
        hull_area = cv2.contourArea(hull)
        result["areaRatio"] = hull_area / contour_area if contour_area > 0 else 0.0
        perim = cv2.arcLength(hull, True)
        circularity = (math.sqrt(4 * math.pi * hull_area) / perim) if perim > 0 else 0.0
        result["deformability"] = 1.0 - circularity
        result["area"] = hull_area
        result["contourUsed"] = "inner"
        if inner_filtered_indices:
            result["selectedFilteredIndex"] = int(inner_filtered_indices[0])
        if parent_indices and parent_indices[0] >= 0 and parent_indices[0] < len(filtered):
            result["selectedParentFilteredIndex"] = int(parent_indices[0])
            result["ringRatio"] = calculate_ring_ratio(c, filtered[parent_indices[0]])

        result["selectedContourArea"] = float(contour_area)
        result["selectedHullArea"] = float(hull_area)
        area_ok = not config["enable_area_range_check"] or (config["area_threshold_min"] <= hull_area <= config["area_threshold_max"])
        ring_ok = (result["ringRatio"] > RING_RATIO_MIN and result["ringRatio"] < RING_RATIO_MAX)
        deform_ok = not config["enable_deformability_range_check"] or (config["deformability_threshold_min"] <= result["deformability"] <= config["deformability_threshold_max"])
        result["passAreaCheck"] = bool(area_ok)
        result["passRingCheck"] = bool(ring_ok)
        result["passDeformabilityCheck"] = bool(deform_ok)
        if area_ok and ring_ok and deform_ok:
            result["inRange"] = True
            result["isValid"] = True
            result["rejectReason"] = REASON_VALID
            result["failedAt"] = ""
        else:
            if not area_ok:
                result["rejectReason"] = REASON_AREA_OOR
                result["failedAt"] = REASON_AREA_OOR
            elif not ring_ok:
                result["rejectReason"] = REASON_RING_OOR
                result["failedAt"] = REASON_RING_OOR
            else:
                result["rejectReason"] = REASON_DEFORM_OOR
                result["failedAt"] = REASON_DEFORM_OOR
    elif filtered and not config["require_single_inner_contour"]:
        idx = max(range(len(filtered)), key=lambda i: cv2.contourArea(filtered[i]))
        c = filtered[idx]
        contour_area = cv2.contourArea(c)
        hull = cv2.convexHull(c)
        hull_area = cv2.contourArea(hull)
        result["areaRatio"] = hull_area / contour_area if contour_area > 0 else 0.0
        perim = cv2.arcLength(hull, True)
        circularity = (math.sqrt(4 * math.pi * hull_area) / perim) if perim > 0 else 0.0
        result["deformability"] = 1.0 - circularity
        result["area"] = hull_area
        result["contourUsed"] = "largest_outer"
        result["selectedFilteredIndex"] = int(idx)
        result["selectedContourArea"] = float(contour_area)
        result["selectedHullArea"] = float(hull_area)
        area_ok = (not config["enable_area_range_check"]) or (config["area_threshold_min"] <= hull_area <= config["area_threshold_max"])
        deform_ok = not config["enable_deformability_range_check"] or (config["deformability_threshold_min"] <= result["deformability"] <= config["deformability_threshold_max"])
        result["passAreaCheck"] = bool(area_ok)
        # Ring ratio check not used on this path (keep true to indicate not the cause of rejection)
        result["passRingCheck"] = True
        result["passDeformabilityCheck"] = bool(deform_ok)
        if area_ok and deform_ok:
            result["inRange"] = True
            result["isValid"] = True
            result["rejectReason"] = REASON_VALID
            result["failedAt"] = ""
        else:
            if not area_ok:
                result["rejectReason"] = REASON_AREA_OOR
                result["failedAt"] = REASON_AREA_OOR
            else:
                result["rejectReason"] = REASON_DEFORM_OOR
                result["failedAt"] = REASON_DEFORM_OOR
    else:
        # No metrics path (e.g., require_single_inner_contour is true, but earlier return handled;
        # or filtered exists but no allowed contour selection). Be explicit.
        result["rejectReason"] = REASON_NO_SINGLE_INNER if config["require_single_inner_contour"] else REASON_NO_CONTOURS
        result["failedAt"] = result["rejectReason"]

    return result


def _contour_touches_roi_border(c: np.ndarray, roi: tuple[int, int, int, int]) -> bool:
    rx, ry, rw, rh = roi
    for pt in c[:, 0, :]:
        x, y = int(pt[0]) - rx, int(pt[1]) - ry
        if 0 <= x < rw and 0 <= y < rh:
            if x < BORDER_THRESHOLD or x >= rw - BORDER_THRESHOLD or y < BORDER_THRESHOLD or y >= rh - BORDER_THRESHOLD:
                return True
        else:
            return True
    return False


def _contour_brightness_quantiles(gray: np.ndarray, contour: np.ndarray) -> tuple[float, float, float, float]:
    if not gray.size:
        return 0.0, 0.0, 0.0, 0.0
    contour_mask = np.zeros(gray.shape[:2], dtype=np.uint8)
    cv2.drawContours(contour_mask, [contour], -1, 255, -1)
    return brightness_quantiles(gray, contour_mask)


def metrics_for_detected_objects(
    mask: np.ndarray,
    roi: tuple[int, int, int, int],
    config: dict[str, Any],
    original_gray: np.ndarray,
) -> list[dict[str, Any]]:
    """
    Compute one metrics row per detected object candidate.

    A candidate is an inner contour with its parent outer contour when available.
    This is intended for full-frame recording files where multiple objects may
    appear in the ROI and a single frame-level row would collapse or reject them.
    """
    filtered, inner_contours, parent_indices, all_contours, _, inner_filtered_indices = find_contours_filtered(mask)
    object_count = len(inner_contours)
    if object_count == 0:
        frame_result = filter_processed_image(mask, roi, config, original_gray)
        frame_result["objectId"] = -1
        frame_result["objectCount"] = 0
        frame_result["metricsMode"] = "objects"
        return [frame_result]

    rows: list[dict[str, Any]] = []
    for object_id, c in enumerate(inner_contours):
        parent_idx = parent_indices[object_id] if object_id < len(parent_indices) else -1
        contour_area = cv2.contourArea(c)
        hull = cv2.convexHull(c)
        hull_area = cv2.contourArea(hull)
        perim = cv2.arcLength(hull, True)
        circularity = (math.sqrt(4 * math.pi * hull_area) / perim) if perim > 0 else 0.0
        ring_ratio = 0.0
        if 0 <= parent_idx < len(filtered):
            ring_ratio = calculate_ring_ratio(c, filtered[parent_idx])

        touches_border = _contour_touches_roi_border(c, roi) if config["enable_border_check"] else False
        area_ok = not config["enable_area_range_check"] or (config["area_threshold_min"] <= hull_area <= config["area_threshold_max"])
        ring_ok = ring_ratio > RING_RATIO_MIN and ring_ratio < RING_RATIO_MAX
        deformability = 1.0 - circularity
        deform_ok = not config["enable_deformability_range_check"] or (
            config["deformability_threshold_min"] <= deformability <= config["deformability_threshold_max"]
        )
        valid = (not touches_border) and area_ok and ring_ok and deform_ok

        q1, q2, q3, q4 = _contour_brightness_quantiles(original_gray, c)
        selected_idx = inner_filtered_indices[object_id] if object_id < len(inner_filtered_indices) else -1
        result = {
            "isValid": bool(valid),
            "touchesBorder": bool(touches_border),
            "hasSingleInnerContour": True,
            "inRange": bool(valid),
            "innerContourCount": object_count,
            "deformability": deformability,
            "area": hull_area,
            "areaRatio": hull_area / contour_area if contour_area > 0 else 0.0,
            "ringRatio": ring_ratio,
            "brightness_q1": q1,
            "brightness_q2": q2,
            "brightness_q3": q3,
            "brightness_q4": q4,
            "rejectReason": REASON_VALID if valid else "",
            "failedAt": "",
            "passSingleInnerCheck": True,
            "passBorderCheck": not touches_border,
            "passAreaCheck": bool(area_ok),
            "passRingCheck": bool(ring_ok),
            "passDeformabilityCheck": bool(deform_ok),
            "contourUsed": "inner",
            "allContourCount": int(len(all_contours)) if all_contours is not None else 0,
            "filteredContourCount": int(len(filtered)),
            "selectedFilteredIndex": int(selected_idx),
            "selectedParentFilteredIndex": int(parent_idx),
            "selectedContourArea": float(contour_area),
            "selectedHullArea": float(hull_area),
            "objectId": int(object_id),
            "objectCount": int(object_count),
            "metricsMode": "objects",
        }
        if not valid:
            if touches_border:
                result["rejectReason"] = REASON_TOUCHES_BORDER
                result["failedAt"] = REASON_TOUCHES_BORDER
            elif not area_ok:
                result["rejectReason"] = REASON_AREA_OOR
                result["failedAt"] = REASON_AREA_OOR
            elif not ring_ok:
                result["rejectReason"] = REASON_RING_OOR
                result["failedAt"] = REASON_RING_OOR
            else:
                result["rejectReason"] = REASON_DEFORM_OOR
                result["failedAt"] = REASON_DEFORM_OOR
        rows.append(result)
    return rows


def draw_overlay(
    original: np.ndarray,
    mask: np.ndarray,
    all_contours: list,
    filtered_contours: list,
    trace: dict[str, Any],
) -> np.ndarray:
    out = cv2.cvtColor(original, cv2.COLOR_GRAY2BGR) if len(original.shape) == 2 else original.copy()
    if out.shape[2] == 1:
        out = cv2.cvtColor(out, cv2.COLOR_GRAY2BGR)

    # Draw all raw contours (thin) in blue
    for c in all_contours:
        cv2.drawContours(out, [c], -1, (255, 0, 0), 1)

    # Highlight inner contour (green) and outer contour (red), thicker
    sel_idx = int(trace.get("selectedFilteredIndex", -1))
    parent_idx = int(trace.get("selectedParentFilteredIndex", -1))
    if 0 <= parent_idx < len(filtered_contours):
        cv2.drawContours(out, [filtered_contours[parent_idx]], -1, (0, 0, 255), 1)  # BGR red (outer)
    if 0 <= sel_idx < len(filtered_contours):
        cv2.drawContours(out, [filtered_contours[sel_idx]], -1, (0, 255, 0), 1)  # BGR green (inner)

    # Label (reason + contour used)
    reason = str(trace.get("rejectReason", ""))
    contour_used = str(trace.get("contourUsed", ""))
    idx = trace.get("index", "")
    rr = trace.get("ringRatio", 0.0)
    area = trace.get("area", 0.0)
    line = f"idx={idx} reason={reason} contour={contour_used} ring={rr:.3f} area={area:.1f}"
    cv2.putText(out, line, (8, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1, cv2.LINE_AA)

    return out


def process_one_frame_set(
    h5_file: h5py.File,
    frame_type: str,
    config: dict[str, Any],
    output_dir: Path,
    args: argparse.Namespace,
    bg_blurred: Optional[np.ndarray],
    blur_k: int,
    roi: tuple[int, int, int, int],
) -> tuple[list[dict], list[np.ndarray], list[np.ndarray]]:
    """Process one HDF5 frame group. roi = (x, y, w, h). Returns metrics and export-h5 payloads."""
    path_prefix = FRAME_PATHS[frame_type]
    images_ds = path_prefix + "/images"
    meta_ds = path_prefix + "/metadata"
    if images_ds not in h5_file or meta_ds not in h5_file:
        return [], [], []

    meta = read_hdf5_metadata(h5_file, meta_ds)
    if meta is None:
        return [], [], []

    images = h5_file[images_ds]
    n = len(meta)
    metrics_list = []
    masks_list = []
    images_list = []
    rx, ry, rw, rh = roi
    object_metrics = args.metrics_mode == "objects" or (
        args.metrics_mode == "auto" and frame_type == "recorded" and not args.export_h5
    )

    for i in range(n):
        frame_index = int(meta[i]["index"])
        timestamp_ns = int(meta[i]["timestampNs"])
        img = images[i]
        gray = _ensure_grayscale(np.asarray(img))
        if gray.dtype != np.uint8 and gray.size > 0:
            mx = gray.max()
            if mx > 255:
                gray = (gray.astype(np.float64) / mx * 255).astype(np.uint8)
            else:
                gray = gray.astype(np.uint8)

        h_img, w_img = gray.shape[0], gray.shape[1]
        use_roi_crop = not (rx == 0 and ry == 0 and rw == w_img and rh == h_img)
        if not use_roi_crop:
            blurred, diff, thresh, mask = run_pipeline(gray, bg_blurred, config)
        else:
            gray_roi = gray[ry : ry + rh, rx : rx + rw]
            bg_roi = None
            if bg_blurred is not None and bg_blurred.shape == gray.shape:
                bg_roi = bg_blurred[ry : ry + rh, rx : rx + rw]
            blurred_roi, diff_roi, thresh_roi, mask_roi = run_pipeline(gray_roi, bg_roi, config)
            blurred = np.zeros_like(gray)
            diff = np.zeros_like(gray)
            thresh = np.zeros_like(gray)
            mask = np.zeros_like(gray)
            blurred[ry : ry + rh, rx : rx + rw] = blurred_roi
            diff[ry : ry + rh, rx : rx + rw] = diff_roi
            thresh[ry : ry + rh, rx : rx + rw] = thresh_roi
            mask[ry : ry + rh, rx : rx + rw] = mask_roi

        rec_metrics = filter_processed_image(mask, roi, config, gray) if (args.export_csv or args.export_h5 or args.save_overlay) else {}
        rec_metrics["index"] = frame_index
        rec_metrics["timestampNs"] = timestamp_ns
        if object_metrics and args.export_csv:
            frame_rows = metrics_for_detected_objects(mask, roi, config, gray)
            for row in frame_rows:
                row["index"] = frame_index
                row["timestampNs"] = timestamp_ns
            metrics_list.extend(frame_rows)
        else:
            rec_metrics["objectId"] = 0 if rec_metrics.get("contourUsed") != "none" else -1
            rec_metrics["objectCount"] = rec_metrics.get("innerContourCount", 0)
            rec_metrics["metricsMode"] = "frame"
            metrics_list.append(rec_metrics)
        if args.export_h5:
            masks_list.append(mask.copy())
            images_list.append(gray.copy())

        if args.save_intermediates:
            frame_dir = output_dir / frame_type / f"frame_{frame_index:06d}"
            frame_dir.mkdir(parents=True, exist_ok=True)
            _save_tiff(frame_dir / "original.tiff", gray)
            _save_tiff(frame_dir / "blurred.tiff", blurred)
            _save_tiff(frame_dir / "diff.tiff", diff)
            _save_tiff(frame_dir / "thresh.tiff", thresh)
            _save_tiff(frame_dir / "mask.tiff", mask)
            if args.save_overlay:
                filtered, _, _, all_contours, _, _ = find_contours_filtered(mask)
                overlay = draw_overlay(gray, mask, list(all_contours), filtered, rec_metrics)
                _save_tiff(frame_dir / "overlay.tiff", overlay)

    return metrics_list, masks_list, images_list


def write_csv(
    rows_valid: list[dict],
    rows_invalid: list[dict],
    rows_recorded: list[dict],
    output_path: Path,
    pixel_to_micron: float,
    frame_type: str,
    roi: tuple[int, int, int, int],
) -> None:
    area_factor = pixel_to_micron * pixel_to_micron
    roi_x, roi_y, roi_w, roi_h = roi
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("Frame Type,Index,Timestamp,Deformability,Area,Area (um²),Area Ratio,Ring Ratio,")
        f.write("Valid,Touches Border,Single Inner,In Range,Inner Count,")
        f.write("Bright Q1,Bright Q2,Bright Q3,Bright Q4,")
        # Reanalysis visibility fields
        f.write("Reject Reason,Failed At,Contour Used,")
        f.write("Filtered Contours,All Contours,Selected Idx,Selected Parent Idx,")
        f.write("Selected Contour Area,Selected Hull Area,")
        f.write("Pass Single Inner,Pass Border,Pass Area,Pass Ring,")
        f.write("Object Id,Object Count,Metrics Mode,")
        f.write("ROI X,ROI Y,ROI W,ROI H\n")

        def write_row(ft: str, r: dict) -> None:
            def yn(v: Any) -> str:
                return "Yes" if bool(v) else "No"

            area_um = r.get("area", 0) * area_factor
            f.write(f"{ft},{r['index']},{r['timestampNs']},{r.get('deformability', 0):.3f},{r.get('area', 0):.2f},{area_um:.2f},{r.get('areaRatio', 0):.3f},{r.get('ringRatio', 0):.3f},")
            f.write(f"{yn(r.get('isValid'))},")
            f.write(f"{yn(r.get('touchesBorder'))},")
            f.write(f"{yn(r.get('hasSingleInnerContour'))},")
            f.write(f"{yn(r.get('inRange'))},")
            f.write(f"{r.get('innerContourCount', 0)},")
            f.write(f"{r.get('brightness_q1', 0):.2f},{r.get('brightness_q2', 0):.2f},{r.get('brightness_q3', 0):.2f},{r.get('brightness_q4', 0):.2f},")

            f.write(f"{r.get('rejectReason', '')},{r.get('failedAt', '')},{r.get('contourUsed', '')},")
            f.write(f"{r.get('filteredContourCount', 0)},{r.get('allContourCount', 0)},")
            f.write(f"{r.get('selectedFilteredIndex', -1)},{r.get('selectedParentFilteredIndex', -1)},")
            f.write(f"{r.get('selectedContourArea', 0.0):.2f},{r.get('selectedHullArea', 0.0):.2f},")
            f.write(f"{yn(r.get('passSingleInnerCheck'))},{yn(r.get('passBorderCheck'))},{yn(r.get('passAreaCheck'))},{yn(r.get('passRingCheck'))},")
            f.write(f"{r.get('objectId', -1)},{r.get('objectCount', 0)},{r.get('metricsMode', 'frame')},")
            f.write(f"{roi_x},{roi_y},{roi_w},{roi_h}\n")

        if frame_type in ("valid", "both", "all"):
            for r in rows_valid:
                write_row("Valid", r)
        if frame_type in ("invalid", "both", "all"):
            for r in rows_invalid:
                write_row("Invalid", r)
        if frame_type in ("recorded", "all") or (frame_type == "both" and rows_recorded):
            for r in rows_recorded:
                write_row("Recorded", r)


def write_reanalysis_h5(
    output_path: Path,
    valid_images: list[np.ndarray],
    valid_masks: list[np.ndarray],
    valid_metrics: list[dict],
    invalid_images: list[np.ndarray],
    invalid_masks: list[np.ndarray],
    invalid_metrics: list[dict],
    exp_info: Optional[dict],
) -> None:
    dtype_meta = np.dtype([
        ("index", np.uint64),
        ("timestampNs", np.uint64),
        ("deformability", np.float64),
        ("area", np.float64),
        ("areaRatio", np.float64),
        ("ringRatio", np.float64),
        ("isValid", np.uint8),
        ("touchesBorder", np.uint8),
        ("hasSingleInnerContour", np.uint8),
        ("inRange", np.uint8),
        ("innerContourCount", np.int32),
        ("brightness_q1", np.float64),
        ("brightness_q2", np.float64),
        ("brightness_q3", np.float64),
        ("brightness_q4", np.float64),
    ])

    def to_meta_row(d: dict) -> tuple:
        return (
            d["index"],
            d["timestampNs"],
            d.get("deformability", 0.0),
            d.get("area", 0.0),
            d.get("areaRatio", 0.0),
            d.get("ringRatio", 0.0),
            1 if d.get("isValid") else 0,
            1 if d.get("touchesBorder") else 0,
            1 if d.get("hasSingleInnerContour") else 0,
            1 if d.get("inRange") else 0,
            d.get("innerContourCount", 0),
            d.get("brightness_q1", 0.0),
            d.get("brightness_q2", 0.0),
            d.get("brightness_q3", 0.0),
            d.get("brightness_q4", 0.0),
        )

    with h5py.File(output_path, "w") as f:
        if exp_info:
            g = f.create_group("experiment_info")
            for k, v in exp_info.items():
                try:
                    g.attrs[k] = v
                except (TypeError, ValueError):
                    g.attrs[k] = str(v)
        for name, imgs, masks, metrics in [
            ("valid", valid_images, valid_masks, valid_metrics),
            ("invalid", invalid_images, invalid_masks, invalid_metrics),
        ]:
            if not imgs:
                continue
            grp_name = "valid_frames" if name == "valid" else "invalid_frames"
            grp = f.create_group(grp_name)
            stack = np.stack([np.asarray(x) for x in imgs])
            grp.create_dataset("images", data=stack, compression="gzip")
            mask_stack = np.stack([np.asarray(m) for m in masks])
            grp.create_dataset("masks", data=mask_stack, compression="gzip")
            meta_arr = np.array([to_meta_row(m) for m in metrics], dtype=dtype_meta)
            grp.create_dataset("metadata", data=meta_arr)


def process_hdf5_file(input_path: Path, output_dir: Path, args: argparse.Namespace) -> int:
    config = load_processing_config(args)
    blur_k = _to_odd(config["gaussian_blur_size"])

    try:
        with h5py.File(input_path, "r") as h5_file:
            exp_info = read_experiment_info(h5_file)
            recording_info = read_group_attrs(h5_file, "/recording_info")
            frame_types = selected_frame_types(h5_file, args.frame_type)
            if not frame_types:
                available = ", ".join(available_frame_types(h5_file)) or "none"
                print(
                    f"ERROR: No matching frame groups for --frame-type {args.frame_type!r} in {input_path} (available: {available})",
                    file=sys.stderr,
                )
                return 1
            img_shape = get_image_shape_from_h5(h5_file)
            if img_shape is None:
                print("ERROR: No images found in .h5 (valid_frames, invalid_frames, or recorded_frames)", file=sys.stderr)
                return 1
            img_h, img_w = img_shape
            roi = extract_roi_from_exp_info(exp_info, img_w, img_h)
            if roi is None:
                roi = compute_synthetic_roi_from_valid_masks(h5_file, img_w, img_h)
                roi_origin = "synthetic" if roi != (0, 0, img_w, img_h) else "full_frame"
            else:
                roi_origin = "stored"
            print(f"ROI: {roi} (source: {roi_origin})")

            backgrounds: dict[str, Optional[np.ndarray]] = {frame_type: None for frame_type in frame_types}
            background_mode = args.background
            if background_mode == "stored":
                bg_path_h5 = "/experiment_info/background"
                if bg_path_h5 in h5_file:
                    ds = h5_file[bg_path_h5]
                    bg_raw = np.asarray(ds)
                    if bg_raw.size == 0:
                        print("WARNING: /experiment_info/background is empty; falling back to --background from_all", file=sys.stderr)
                        background_mode = "from_all"
                    else:
                        if bg_raw.ndim == 3 and bg_raw.shape[0] == 1:
                            bg_img = np.squeeze(bg_raw, 0)
                        else:
                            bg_img = bg_raw
                        bg_img = _ensure_grayscale(bg_img.astype(np.float64) if bg_img.dtype != np.uint8 else bg_img)
                        if bg_img.dtype != np.uint8 and bg_img.size > 0:
                            bg_img = (bg_img.astype(np.float64) / bg_img.max() * 255).astype(np.uint8)
                        bg_blurred = cv2.GaussianBlur(bg_img, (blur_k, blur_k), 0)
                        for frame_type in frame_types:
                            backgrounds[frame_type] = bg_blurred.copy()
                else:
                    print("WARNING: No stored background in .h5 (/experiment_info/background); falling back to --background from_all", file=sys.stderr)
                    background_mode = "from_all"
            if background_mode == "from_all":
                for frame_type in frame_types:
                    backgrounds[frame_type] = build_background_from_all_images(
                        h5_file, FRAME_PATHS[frame_type], config, blur_k
                    )
                    if backgrounds[frame_type] is not None:
                        print(f"Built background from all {frame_type} frame images (pixel-wise mean)")
            elif background_mode != "none" and background_mode != "stored":
                bg_path = Path(background_mode)
                if bg_path.exists():
                    bg_img = cv2.imread(str(bg_path), cv2.IMREAD_GRAYSCALE)
                    if bg_img is not None:
                        bg_blurred = cv2.GaussianBlur(bg_img, (blur_k, blur_k), 0)
                        for frame_type in frame_types:
                            backgrounds[frame_type] = bg_blurred.copy()

            all_metrics_valid, all_masks_valid, all_images_valid = [], [], []
            all_metrics_invalid, all_masks_invalid, all_images_invalid = [], [], []
            all_metrics_recorded, all_masks_recorded, all_images_recorded = [], [], []

            if "valid" in frame_types:
                m, masks, imgs = process_one_frame_set(
                    h5_file, "valid", config, output_dir, args, backgrounds.get("valid"), blur_k, roi
                )
                all_metrics_valid, all_masks_valid, all_images_valid = m, masks, imgs
                print(f"Processed {len(m)} valid metric rows -> {output_dir / 'valid'}")
            if "invalid" in frame_types:
                m, masks, imgs = process_one_frame_set(
                    h5_file, "invalid", config, output_dir, args, backgrounds.get("invalid"), blur_k, roi
                )
                all_metrics_invalid, all_masks_invalid, all_images_invalid = m, masks, imgs
                print(f"Processed {len(m)} invalid metric rows -> {output_dir / 'invalid'}")
            if "recorded" in frame_types:
                m, masks, imgs = process_one_frame_set(
                    h5_file, "recorded", config, output_dir, args, backgrounds.get("recorded"), blur_k, roi
                )
                all_metrics_recorded, all_masks_recorded, all_images_recorded = m, masks, imgs
                print(f"Processed {len(m)} recorded metric rows -> {output_dir / 'recorded'}")

            if args.export_csv and (all_metrics_valid or all_metrics_invalid or all_metrics_recorded):
                csv_path = output_dir / "metrics.csv"
                write_csv(
                    all_metrics_valid,
                    all_metrics_invalid,
                    all_metrics_recorded,
                    csv_path,
                    args.pixel_to_micron,
                    args.frame_type,
                    roi,
                )
                print(f"Wrote {csv_path}")

            if args.export_h5 and (all_images_valid or all_images_invalid or all_images_recorded):
                h5_path = output_dir / "reanalysis.h5"
                exp_info_for_h5 = dict(exp_info) if exp_info else {}
                if recording_info:
                    exp_info_for_h5["source_mode"] = "frame_recording_reanalysis"
                    for k, v in recording_info.items():
                        exp_info_for_h5[f"recording_{k}"] = v
                exp_info_for_h5["roi_x"] = roi[0]
                exp_info_for_h5["roi_y"] = roi[1]
                exp_info_for_h5["roi_w"] = roi[2]
                exp_info_for_h5["roi_h"] = roi[3]
                export_valid_metrics = list(all_metrics_valid)
                export_valid_masks = list(all_masks_valid)
                export_valid_images = list(all_images_valid)
                export_invalid_metrics = list(all_metrics_invalid)
                export_invalid_masks = list(all_masks_invalid)
                export_invalid_images = list(all_images_invalid)
                for i, metrics in enumerate(all_metrics_recorded):
                    if metrics.get("isValid"):
                        export_valid_metrics.append(metrics)
                        export_valid_masks.append(all_masks_recorded[i])
                        export_valid_images.append(all_images_recorded[i])
                    else:
                        export_invalid_metrics.append(metrics)
                        export_invalid_masks.append(all_masks_recorded[i])
                        export_invalid_images.append(all_images_recorded[i])
                write_reanalysis_h5(
                    h5_path,
                    export_valid_images, export_valid_masks, export_valid_metrics,
                    export_invalid_images, export_invalid_masks, export_invalid_metrics,
                    exp_info_for_h5,
                )
                print(f"Wrote {h5_path}")

    except IOError as e:
        print(f"ERROR: Failed to open HDF5 file: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1

    print(f"Reanalysis complete. Output: {output_dir}")
    return 0


def main() -> int:
    if getattr(sys, "frozen", False) and len(sys.argv) == 1:
        _interactive_prompt_args()
    args = parse_args()
    input_path = Path(args.input)
    output_dir = Path(args.output)
    if not input_path.exists():
        print(f"ERROR: Input path does not exist: {input_path}", file=sys.stderr)
        return 1
    output_dir.mkdir(parents=True, exist_ok=True)

    if input_path.is_file():
        return process_hdf5_file(input_path, output_dir, args)

    hdf5_files = sorted(
        p for p in input_path.rglob("*")
        if p.is_file() and p.suffix.lower() in {".h5", ".hdf5"}
    )
    if not hdf5_files:
        print(f"ERROR: No .h5/.hdf5 files found under {input_path}", file=sys.stderr)
        return 1

    failures = 0
    print(f"Found {len(hdf5_files)} HDF5 files under {input_path}")
    for h5_path in hdf5_files:
        rel = h5_path.relative_to(input_path).with_suffix("")
        per_file_output = output_dir / rel
        per_file_output.mkdir(parents=True, exist_ok=True)
        print(f"\n=== Reanalysing {h5_path} ===")
        rc = process_hdf5_file(h5_path, per_file_output, args)
        if rc != 0:
            failures += 1

    if failures:
        print(f"Completed with {failures} failed file(s). Output: {output_dir}", file=sys.stderr)
        return 1
    print(f"Reanalysis complete for {len(hdf5_files)} files. Output: {output_dir}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SystemExit as e:
        # When double-clicked, the console closes immediately; pause so the user can read the message
        if getattr(sys, "frozen", False) and e.code != 0:
            input("\nPress Enter to close...")
        raise
