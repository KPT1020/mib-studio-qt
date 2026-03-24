"""
Node functions for the empty frame detection Kedro pipeline.

Refactored from scripts/empty_frame_detection.py — processing logic is
identical to the C++ ProcessingService to maintain parity.
"""

from __future__ import annotations

import logging
import os
import shutil
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import cv2
import numpy as np
from datasets import load_dataset
from sklearn.metrics import (
    accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
    precision_score,
    recall_score,
)

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Processing defaults (match ProcessingService / reanalyse_hdf5.py)
# ---------------------------------------------------------------------------
DEFAULT_BLUR = 3
DEFAULT_THRESHOLD = 8
DEFAULT_MORPH_KERNEL = 3
DEFAULT_MORPH_ITERATIONS = 1
MIN_NOISE_AREA = 100.0
DEFAULT_BAND_FRACTION = 0.5

# Label mapping: dataset class names -> binary (0 = empty, 1 = non-empty)
EMPTY_LABELS = {"empty", "debris"}


@dataclass
class ProcessingConfig:
    gaussian_blur_size: int = DEFAULT_BLUR
    bg_subtract_threshold: int = DEFAULT_THRESHOLD
    morph_kernel_size: int = DEFAULT_MORPH_KERNEL
    morph_iterations: int = DEFAULT_MORPH_ITERATIONS
    min_contour_area: float = MIN_NOISE_AREA
    band_fraction: float = DEFAULT_BAND_FRACTION


@dataclass
class FrameResult:
    """Holds all intermediate images from the processing pipeline."""

    is_empty: bool
    contours: list
    gray: np.ndarray  # original grayscale
    diff: np.ndarray  # background-subtracted
    thresh: np.ndarray  # binary threshold (full frame)
    mask: np.ndarray  # morphology output (full frame)
    mask_band: np.ndarray  # band-cropped mask


# ---------------------------------------------------------------------------
# Processing helpers (verbatim from scripts/empty_frame_detection.py)
# ---------------------------------------------------------------------------


def _to_odd(v: int) -> int:
    if v < 1:
        v = 1
    if (v % 2) == 0:
        v += 1
    return v


def ensure_grayscale(img: np.ndarray) -> np.ndarray:
    """Convert image to single-channel uint8 grayscale."""
    if img.dtype != np.uint8:
        if img.size == 0:
            return img.astype(np.uint8)
        mx = img.max()
        if mx > 255:
            img = (img.astype(np.float64) / mx * 255).astype(np.uint8)
        else:
            img = img.astype(np.uint8)
    if img.ndim == 2:
        return np.ascontiguousarray(img)
    if img.ndim == 3 and img.shape[2] == 1:
        return np.ascontiguousarray(img[:, :, 0])
    if img.ndim == 3:
        return cv2.cvtColor(img, cv2.COLOR_RGB2GRAY)
    return img


def pil_to_numpy(pil_img) -> np.ndarray:
    """Convert a PIL Image to a numpy array (RGB or grayscale)."""
    return np.array(pil_img)


def build_background_from_images(images: list[np.ndarray]) -> np.ndarray:
    """Build background by averaging a list of grayscale uint8 images."""
    if not images:
        raise ValueError("No images provided to build background")
    h, w = images[0].shape[:2]
    acc = np.zeros((h, w), dtype=np.float64)
    count = 0
    for img in images:
        gray = ensure_grayscale(img)
        if gray.shape[0] != h or gray.shape[1] != w:
            continue
        acc += gray.astype(np.float64)
        count += 1
    if count == 0:
        raise ValueError("No valid images for background (shape mismatch)")
    return np.clip(acc / count, 0, 255).astype(np.uint8)


def process_frame(
    image: np.ndarray,
    background: np.ndarray,
    config: ProcessingConfig,
) -> FrameResult:
    """Process a single frame through the pipeline."""
    gray = ensure_grayscale(image)
    h, w = gray.shape[:2]
    blur_k = _to_odd(config.gaussian_blur_size)
    morph_k = _to_odd(config.morph_kernel_size)

    # 1. Gaussian blur
    blurred = cv2.GaussianBlur(gray, (blur_k, blur_k), 0)
    blurred_bg = cv2.GaussianBlur(background, (blur_k, blur_k), 0)

    # 2. Background subtraction
    diff = cv2.subtract(blurred, blurred_bg)

    # 3. Binary threshold
    _, thresh = cv2.threshold(
        diff, config.bg_subtract_threshold, 255, cv2.THRESH_BINARY
    )

    # 4. Morphology: close then open
    kernel = cv2.getStructuringElement(cv2.MORPH_CROSS, (morph_k, morph_k))
    mask = cv2.morphologyEx(
        thresh, cv2.MORPH_CLOSE, kernel, iterations=config.morph_iterations
    )
    mask = cv2.morphologyEx(
        mask, cv2.MORPH_OPEN, kernel, iterations=config.morph_iterations
    )

    # 5. Crop to middle horizontal band
    band_margin = int(h * (1.0 - config.band_fraction) / 2.0)
    y_start = band_margin
    y_end = h - band_margin
    mask_band = mask[y_start:y_end, :]

    # 6. Contour detection on the band
    contours_raw, _ = cv2.findContours(
        mask_band, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
    )

    # 7. Filter by minimum area
    contours = [
        c for c in contours_raw if cv2.contourArea(c) >= config.min_contour_area
    ]

    # 8. Classify
    is_empty = len(contours) == 0

    return FrameResult(
        is_empty=is_empty,
        contours=contours,
        gray=gray,
        diff=diff,
        thresh=thresh,
        mask=mask,
        mask_band=mask_band,
    )


# ---------------------------------------------------------------------------
# Node 1: Load dataset
# ---------------------------------------------------------------------------


def load_dataset_node(dataset_name: str) -> dict:
    """Load HuggingFace dataset and separate images by label."""
    logger.info("Loading dataset %s ...", dataset_name)
    dataset = load_dataset(dataset_name, split="train")
    label_feature = dataset.features["label"]
    label_id_to_name = {i: name for i, name in enumerate(label_feature.names)}

    images_by_label: dict[str, list[np.ndarray]] = {}
    all_images: list[np.ndarray] = []
    all_labels: list[str] = []

    for sample in dataset:
        img = pil_to_numpy(sample["image"])
        label_name = label_id_to_name[sample["label"]]
        all_images.append(img)
        all_labels.append(label_name)
        images_by_label.setdefault(label_name, []).append(img)

    logger.info(
        "Loaded %d images, classes: %s", len(all_images), label_feature.names
    )
    for name in label_feature.names:
        count = len(images_by_label.get(name, []))
        logger.info("  %s: %d", name, count)

    return {
        "all_images": all_images,
        "all_labels": all_labels,
        "images_by_label": images_by_label,
        "label_names": label_feature.names,
    }


# ---------------------------------------------------------------------------
# Node 2: Build background
# ---------------------------------------------------------------------------


def build_background_node(dataset_bundle: dict) -> np.ndarray:
    """Build background from empty frames."""
    images_by_label = dataset_bundle["images_by_label"]
    all_images = dataset_bundle["all_images"]

    empty_frames = [
        ensure_grayscale(img) for img in images_by_label.get("empty", [])
    ]
    if empty_frames:
        logger.info("Building background from %d empty frames ...", len(empty_frames))
        return build_background_from_images(empty_frames)

    logger.info("No 'empty' frames found, building background from all frames ...")
    return build_background_from_images(
        [ensure_grayscale(img) for img in all_images]
    )


# ---------------------------------------------------------------------------
# Node 3: Process frames
# ---------------------------------------------------------------------------


def process_frames_node(
    dataset_bundle: dict,
    background: np.ndarray,
    processing: dict,
) -> dict:
    """Process all frames through the pipeline, collect predictions."""
    config = ProcessingConfig(
        gaussian_blur_size=processing.get("gaussian_blur_size", DEFAULT_BLUR),
        bg_subtract_threshold=processing.get("bg_subtract_threshold", DEFAULT_THRESHOLD),
        morph_kernel_size=processing.get("morph_kernel_size", DEFAULT_MORPH_KERNEL),
        morph_iterations=processing.get("morph_iterations", DEFAULT_MORPH_ITERATIONS),
        min_contour_area=processing.get("min_contour_area", MIN_NOISE_AREA),
        band_fraction=processing.get("band_fraction", DEFAULT_BAND_FRACTION),
    )

    all_images = dataset_bundle["all_images"]
    all_labels = dataset_bundle["all_labels"]

    logger.info(
        "Processing %d frames (blur=%d, threshold=%d, morph=%dx%d, "
        "min_area=%.1f, band=%.2f) ...",
        len(all_images),
        config.gaussian_blur_size,
        config.bg_subtract_threshold,
        config.morph_kernel_size,
        config.morph_iterations,
        config.min_contour_area,
        config.band_fraction,
    )

    predictions: list[int] = []
    ground_truth: list[int] = []
    frame_results: list[FrameResult] = []

    for img, label_name in zip(all_images, all_labels):
        result = process_frame(img, background, config)
        pred = 0 if result.is_empty else 1  # 0=empty, 1=non-empty
        gt = 0 if label_name in EMPTY_LABELS else 1
        predictions.append(pred)
        ground_truth.append(gt)
        frame_results.append(result)

    logger.info("Processing complete.")
    return {
        "predictions": predictions,
        "ground_truth": ground_truth,
        "frame_results": frame_results,
        "all_images": all_images,
        "all_labels": all_labels,
        "config": config,
    }


# ---------------------------------------------------------------------------
# Node 4: Evaluate
# ---------------------------------------------------------------------------


def evaluate_node(processing_results: dict) -> dict:
    """Compute sklearn classification metrics."""
    predictions = processing_results["predictions"]
    ground_truth = processing_results["ground_truth"]
    all_labels = processing_results["all_labels"]

    acc = accuracy_score(ground_truth, predictions)
    prec = precision_score(ground_truth, predictions, zero_division=0)
    rec = recall_score(ground_truth, predictions, zero_division=0)
    f1 = f1_score(ground_truth, predictions, zero_division=0)

    logger.info("=== Binary Classification: empty (0) vs non-empty (1) ===")
    report = classification_report(
        ground_truth,
        predictions,
        target_names=["empty", "non-empty"],
        zero_division=0,
    )
    logger.info("\n%s", report)

    cm = confusion_matrix(ground_truth, predictions)
    logger.info("Confusion Matrix (rows=true, cols=pred):")
    logger.info("  %12s pred_empty  pred_non-empty", "")
    for i, name in enumerate(["true_empty", "true_non-empty"]):
        row = cm[i] if i < len(cm) else [0, 0]
        logger.info("  %14s  %6d  %6d", name, row[0], row[1])

    # Per-class breakdown
    logger.info("=== Per-Class Breakdown ===")
    for label_name in sorted(set(all_labels)):
        indices = [i for i, ln in enumerate(all_labels) if ln == label_name]
        count = len(indices)
        pred_empty = sum(1 for i in indices if predictions[i] == 0)
        pred_nonempty = sum(1 for i in indices if predictions[i] == 1)
        logger.info(
            "  %s: %d total, %d pred_empty, %d pred_non-empty",
            label_name,
            count,
            pred_empty,
            pred_nonempty,
        )

    return {
        "accuracy": acc,
        "precision": prec,
        "recall": rec,
        "f1": f1,
        "confusion_matrix": cm,
    }


# ---------------------------------------------------------------------------
# Node 5: Save artifacts
# ---------------------------------------------------------------------------


def _save_annotated_image(
    output_path: Path,
    image: np.ndarray,
    contours: list,
    is_empty_pred: bool,
    gt_label: str,
    config: ProcessingConfig,
    index: int,
) -> Path:
    """Save an annotated image with band boundaries, contours, and labels."""
    gray = ensure_grayscale(image)
    h, w = gray.shape[:2]

    vis = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

    band_margin = int(h * (1.0 - config.band_fraction) / 2.0)
    y_start = band_margin
    y_end = h - band_margin
    cv2.line(vis, (0, y_start), (w, y_start), (255, 255, 0), 1)
    cv2.line(vis, (0, y_end), (w, y_end), (255, 255, 0), 1)

    color = (0, 0, 255) if is_empty_pred else (0, 255, 0)
    shifted_contours = []
    for c in contours:
        shifted = c.copy()
        shifted[:, :, 1] += y_start
        shifted_contours.append(shifted)
    cv2.drawContours(vis, shifted_contours, -1, color, 1)

    pred_label = "empty" if is_empty_pred else "non-empty"
    gt_binary = "empty" if gt_label in EMPTY_LABELS else "non-empty"
    correct = pred_label == gt_binary
    status = "OK" if correct else "WRONG"

    text_lines = [
        f"GT: {gt_label} ({gt_binary})",
        f"Pred: {pred_label} | Contours: {len(contours)}",
        f"[{status}]",
    ]
    for i, line in enumerate(text_lines):
        y_pos = 20 + i * 20
        cv2.putText(
            vis, line, (5, y_pos), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1
        )

    filename = f"{index:03d}_{gt_label}_{pred_label}.png"
    filepath = output_path / filename
    cv2.imwrite(str(filepath), vis)
    return filepath


def _save_intermediate_images(
    intermediates_dir: Path,
    result: FrameResult,
    annotated_path: Path,
    gt_label: str,
    index: int,
) -> list[Path]:
    """Save all 6 pipeline stage images for a single frame."""
    subdir = intermediates_dir / f"{index:03d}_{gt_label}"
    subdir.mkdir(parents=True, exist_ok=True)

    saved: list[Path] = []
    stages = [
        ("1_grayscale.png", result.gray),
        ("2_bg_subtracted.png", result.diff),
        ("3_threshold.png", result.thresh),
        ("4_morphology.png", result.mask),
        ("5_band_mask.png", result.mask_band),
    ]
    for name, img in stages:
        path = subdir / name
        cv2.imwrite(str(path), img)
        saved.append(path)

    ann_dest = subdir / "6_annotated.png"
    shutil.copy2(str(annotated_path), str(ann_dest))
    saved.append(ann_dest)

    return saved


def _generate_summary(
    config: ProcessingConfig,
    metrics: dict,
    all_labels: list[str],
    predictions: list[int],
    ground_truth: list[int],
    output_path: Path,
) -> Path:
    """Generate a markdown experiment summary."""
    cm = metrics["confusion_matrix"]
    report = classification_report(
        ground_truth,
        predictions,
        target_names=["empty", "non-empty"],
        zero_division=0,
    )

    class_rows = []
    for label_name in sorted(set(all_labels)):
        indices = [i for i, ln in enumerate(all_labels) if ln == label_name]
        count = len(indices)
        pred_empty = sum(1 for i in indices if predictions[i] == 0)
        pred_nonempty = sum(1 for i in indices if predictions[i] == 1)
        class_rows.append(
            f"| {label_name} | {count} | {pred_empty} | {pred_nonempty} |"
        )

    summary = f"""# Empty Frame Detection - Experiment Summary

## Date
{datetime.now().isoformat()}

## Dataset
- Source: gavinlouuu/dc_ds (HuggingFace)
- Total images: {len(all_labels)}
- Classes: {', '.join(sorted(set(all_labels)))}

## Method
Middle-band contour count with background subtraction

## Parameters
| Parameter | Value |
|---|---|
| gaussian_blur_size | {config.gaussian_blur_size} |
| bg_subtract_threshold | {config.bg_subtract_threshold} |
| morph_kernel_size | {config.morph_kernel_size} |
| morph_iterations | {config.morph_iterations} |
| min_contour_area | {config.min_contour_area} |
| band_fraction | {config.band_fraction} |

## Metrics
| Metric | Value |
|---|---|
| Accuracy | {metrics['accuracy']:.4f} |
| Precision | {metrics['precision']:.4f} |
| Recall | {metrics['recall']:.4f} |
| F1 Score | {metrics['f1']:.4f} |

## Confusion Matrix
|  | Pred Empty | Pred Non-Empty |
|---|---|---|
| True Empty | {cm[0][0]} | {cm[0][1]} |
| True Non-Empty | {cm[1][0]} | {cm[1][1]} |

## Per-Class Breakdown
| Class | Count | Pred Empty | Pred Non-Empty |
|---|---|---|---|
{chr(10).join(class_rows)}

## Classification Report
```
{report}
```
"""
    summary_path = output_path / "experiment_summary.md"
    summary_path.write_text(summary, encoding="utf-8")
    return summary_path


def save_artifacts_node(
    processing_results: dict,
    metrics: dict,
    output_dir: str,
) -> str:
    """Save annotated images, intermediates, and markdown summary to disk."""
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)
    intermediates_dir = out / "intermediates"
    intermediates_dir.mkdir(parents=True, exist_ok=True)

    config: ProcessingConfig = processing_results["config"]
    frame_results: list[FrameResult] = processing_results["frame_results"]
    all_images = processing_results["all_images"]
    all_labels = processing_results["all_labels"]
    predictions = processing_results["predictions"]
    ground_truth = processing_results["ground_truth"]

    logger.info("Saving artifacts to %s ...", out)

    for i, (img, label_name, result) in enumerate(
        zip(all_images, all_labels, frame_results)
    ):
        ann_path = _save_annotated_image(
            out, img, result.contours, result.is_empty, label_name, config, i
        )
        _save_intermediate_images(intermediates_dir, result, ann_path, label_name, i)

    # Save confusion matrix text
    cm_path = out / "confusion_matrix.txt"
    np.savetxt(str(cm_path), metrics["confusion_matrix"], fmt="%d")

    # Generate markdown summary
    summary_path = _generate_summary(
        config, metrics, all_labels, predictions, ground_truth, out
    )

    logger.info("Artifacts saved: %s", out)
    logger.info("Experiment summary: %s", summary_path)

    return str(out)


# ---------------------------------------------------------------------------
# Node 6: Log to MLflow
# ---------------------------------------------------------------------------


def _test_mlflow_client_artifacts(tracking_uri: str) -> bool:
    """Quick test: can the mlflow client upload artifacts without S3 credentials?"""
    import tempfile

    try:
        import mlflow

        mlflow.set_tracking_uri(tracking_uri)
        client = mlflow.MlflowClient()
        run = client.create_run("0")  # default experiment
        rid = run.info.run_id
        with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False) as f:
            f.write("test")
            tmp = f.name
        try:
            client.log_artifact(rid, tmp)
            client.set_terminated(rid, "FINISHED")
            client.delete_run(rid)
            return True
        except Exception:
            client.set_terminated(rid, "FAILED")
            client.delete_run(rid)
            return False
        finally:
            os.unlink(tmp)
    except Exception:
        return False


def _log_with_mlflow_client(
    config: ProcessingConfig,
    metrics: dict,
    output_dir: Path,
    tracking_uri: str,
    experiment_name: str,
    run_name: str,
) -> str:
    """Log experiment using the mlflow Python client. Returns run URL."""
    import mlflow

    mlflow.set_tracking_uri(tracking_uri)
    mlflow.set_experiment(experiment_name)

    with mlflow.start_run(run_name=run_name) as run:
        mlflow.log_params(
            {
                "gaussian_blur_size": config.gaussian_blur_size,
                "bg_subtract_threshold": config.bg_subtract_threshold,
                "morph_kernel_size": config.morph_kernel_size,
                "morph_iterations": config.morph_iterations,
                "min_contour_area": config.min_contour_area,
                "band_fraction": config.band_fraction,
                "dataset": "gavinlouuu/dc_ds",
                "method": "middle_band_contour_count",
            }
        )

        mlflow.log_metrics(
            {
                "accuracy": metrics["accuracy"],
                "precision": metrics["precision"],
                "recall": metrics["recall"],
                "f1": metrics["f1"],
            }
        )

        # Log top-level artifacts (summary, confusion matrix)
        for f in output_dir.iterdir():
            if f.is_file() and f.suffix in (".md", ".txt"):
                mlflow.log_artifact(str(f))

        # Log annotated images
        annotated_files = sorted(output_dir.glob("*.png"))
        if annotated_files:
            logger.info("Uploading %d annotated images ...", len(annotated_files))
            for i, img_path in enumerate(annotated_files):
                mlflow.log_artifact(str(img_path), artifact_path="annotated_images")
                if (i + 1) % 20 == 0 or (i + 1) == len(annotated_files):
                    logger.info("  %d/%d", i + 1, len(annotated_files))

        # Log intermediate images
        intermediates_dir = output_dir / "intermediates"
        if intermediates_dir.is_dir():
            frame_dirs = sorted(
                d for d in intermediates_dir.iterdir() if d.is_dir()
            )
            total = sum(len(list(d.glob("*.png"))) for d in frame_dirs)
            logger.info("Uploading %d intermediate images ...", total)
            uploaded = 0
            for frame_dir in frame_dirs:
                for img_path in sorted(frame_dir.glob("*.png")):
                    mlflow.log_artifact(
                        str(img_path),
                        artifact_path=f"intermediates/{frame_dir.name}",
                    )
                    uploaded += 1
                    if uploaded % 50 == 0 or uploaded == total:
                        logger.info("  %d/%d", uploaded, total)

        run_url = (
            f"{tracking_uri}/#/experiments/"
            f"{run.info.experiment_id}/runs/{run.info.run_id}"
        )
        logger.info("MLflow run logged: %s", run_url)

        # Verify artifacts
        client = mlflow.MlflowClient()
        arts = client.list_artifacts(run.info.run_id)
        if arts:
            logger.info(
                "Artifact verification: %d top-level entries visible", len(arts)
            )
            for a in arts:
                logger.info(
                    "  %s %s", "[dir]" if a.is_dir else "[file]", a.path
                )
        else:
            logger.warning(
                "No artifacts visible in MLflow. "
                "Server may need --serve-artifacts flag."
            )

        return run_url


def _log_with_rest_api(
    config: ProcessingConfig,
    metrics: dict,
    output_dir: Path,
    base_url: str,
    experiment_name: str,
    run_name: str,
) -> str:
    """Fallback: log experiment using raw REST API. Returns run URL."""
    import requests

    auth = None
    username = os.environ.get("MLFLOW_TRACKING_USERNAME")
    password = os.environ.get("MLFLOW_TRACKING_PASSWORD")
    if username and password:
        auth = (username, password)

    def api(method, endpoint, **kwargs):
        url = f"{base_url}/api/2.0/mlflow/{endpoint}"
        resp = getattr(requests, method)(url, auth=auth, **kwargs)
        resp.raise_for_status()
        return resp.json()

    def upload_artifact(
        experiment_id: str,
        run_id: str,
        local_path: Path,
        subdir: str = "",
    ) -> None:
        artifact_path = f"{subdir}/{local_path.name}" if subdir else local_path.name
        url = (
            f"{base_url}/api/2.0/mlflow-artifacts/artifacts/"
            f"{experiment_id}/{run_id}/artifacts/{artifact_path}"
        )
        with open(local_path, "rb") as f:
            resp = requests.put(
                url,
                data=f,
                headers={"Content-Type": "application/octet-stream"},
                auth=auth,
            )
            resp.raise_for_status()

    # Get or create experiment
    try:
        result = api(
            "get",
            "experiments/get-by-name",
            params={"experiment_name": experiment_name},
        )
        experiment_id = result["experiment"]["experiment_id"]
    except requests.HTTPError:
        result = api(
            "post", "experiments/create", json={"name": experiment_name}
        )
        experiment_id = result["experiment_id"]

    # Create run
    run_data = api(
        "post",
        "runs/create",
        json={
            "experiment_id": experiment_id,
            "run_name": run_name,
            "start_time": int(time.time() * 1000),
        },
    )
    run_id = run_data["run"]["info"]["run_id"]

    # Log parameters and metrics
    ts = int(time.time() * 1000)
    api(
        "post",
        "runs/log-batch",
        json={
            "run_id": run_id,
            "params": [
                {
                    "key": "gaussian_blur_size",
                    "value": str(config.gaussian_blur_size),
                },
                {
                    "key": "bg_subtract_threshold",
                    "value": str(config.bg_subtract_threshold),
                },
                {
                    "key": "morph_kernel_size",
                    "value": str(config.morph_kernel_size),
                },
                {
                    "key": "morph_iterations",
                    "value": str(config.morph_iterations),
                },
                {
                    "key": "min_contour_area",
                    "value": str(config.min_contour_area),
                },
                {"key": "band_fraction", "value": str(config.band_fraction)},
                {"key": "dataset", "value": "gavinlouuu/dc_ds"},
                {"key": "method", "value": "middle_band_contour_count"},
            ],
            "metrics": [
                {
                    "key": "accuracy",
                    "value": metrics["accuracy"],
                    "timestamp": ts,
                    "step": 0,
                },
                {
                    "key": "precision",
                    "value": metrics["precision"],
                    "timestamp": ts,
                    "step": 0,
                },
                {
                    "key": "recall",
                    "value": metrics["recall"],
                    "timestamp": ts,
                    "step": 0,
                },
                {
                    "key": "f1",
                    "value": metrics["f1"],
                    "timestamp": ts,
                    "step": 0,
                },
            ],
        },
    )

    # Upload artifacts
    logger.info("Uploading artifacts to MLflow (REST) ...")

    for f in output_dir.iterdir():
        if f.is_file() and f.suffix in (".md", ".txt"):
            upload_artifact(experiment_id, run_id, f)

    annotated_files = sorted(output_dir.glob("*.png"))
    for i, img_path in enumerate(annotated_files):
        upload_artifact(experiment_id, run_id, img_path, subdir="annotated_images")
        if (i + 1) % 20 == 0 or (i + 1) == len(annotated_files):
            logger.info("  Annotated: %d/%d", i + 1, len(annotated_files))

    intermediates_dir = output_dir / "intermediates"
    if intermediates_dir.is_dir():
        frame_dirs = sorted(
            d for d in intermediates_dir.iterdir() if d.is_dir()
        )
        total = sum(len(list(d.glob("*.png"))) for d in frame_dirs)
        uploaded = 0
        for frame_dir in frame_dirs:
            for img_path in sorted(frame_dir.glob("*.png")):
                upload_artifact(
                    experiment_id,
                    run_id,
                    img_path,
                    subdir=f"intermediates/{frame_dir.name}",
                )
                uploaded += 1
                if uploaded % 50 == 0 or uploaded == total:
                    logger.info("  Intermediates: %d/%d", uploaded, total)

    # End run
    api(
        "post",
        "runs/update",
        json={
            "run_id": run_id,
            "status": "FINISHED",
            "end_time": int(time.time() * 1000),
        },
    )

    run_url = f"{base_url}/#/experiments/{experiment_id}/runs/{run_id}"
    logger.info("MLflow run logged: %s", run_url)

    # Verify
    import requests as req

    resp = req.get(
        f"{base_url}/api/2.0/mlflow/artifacts/list",
        params={"run_id": run_id},
        auth=auth,
    )
    if resp.status_code == 200:
        data = resp.json()
        files = data.get("files", [])
        dirs = data.get("root_uri", "")
        logger.info(
            "Artifact verification: root_uri=%s, %d entries", dirs, len(files)
        )
    else:
        logger.warning(
            "Could not verify artifacts (HTTP %d)", resp.status_code
        )

    return run_url


def log_mlflow_node(
    processing_results: dict,
    metrics: dict,
    artifact_dir: str,
    mlflow_config: dict,
) -> str:
    """Log parameters, metrics, and artifacts to MLflow. Returns run URL.

    Tries the MLflow Python client first (auto-reads MLFLOW_TRACKING_USERNAME
    and MLFLOW_TRACKING_PASSWORD env vars). Falls back to REST API if the
    client fails.
    """
    config: ProcessingConfig = processing_results["config"]
    output_dir = Path(artifact_dir)

    tracking_uri = mlflow_config.get(
        "tracking_uri",
        os.environ.get("MLFLOW_TRACKING_URI", "https://mlflow.yofo.bio"),
    )
    experiment_name = mlflow_config.get("experiment_name", "empty-frame-detection")
    run_name = mlflow_config.get("run_name", "middle-band-contour-count")

    # Try MLflow client first (handles auth via env vars automatically)
    try:
        logger.info("Trying MLflow client for logging ...")
        return _log_with_mlflow_client(
            config, metrics, output_dir, tracking_uri, experiment_name, run_name
        )
    except Exception as exc:
        logger.warning(
            "MLflow client failed (%s). Falling back to REST API ...", exc
        )
        return _log_with_rest_api(
            config, metrics, output_dir, tracking_uri, experiment_name, run_name
        )
