"""
Background worker for HDF5 export operations.

This module provides a QObject-based worker that runs export operations
in a background thread, emitting progress signals for GUI updates.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional, Callable

from PySide6.QtCore import QObject, Signal

from export_hdf5 import (
    read_hdf5_metadata,
    read_hdf5_images,
    read_experiment_info,
    export_metrics_to_csv,
    export_images_to_tiff,
    HAS_CV2,
)
import h5py


class ExportWorker(QObject):
    """Worker class for running HDF5 export operations in background thread."""
    
    # Signals for progress updates
    progress_message = Signal(str)  # Status message
    progress_value = Signal(int)  # Progress value (0-100)
    finished = Signal(bool, str)  # (success, message)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self._cancel_requested = False
    
    def cancel(self):
        """Request cancellation of the export operation."""
        self._cancel_requested = True
    
    def run_export(
        self,
        input_path: Path,
        output_dir: Path,
        format_type: str,
        frame_type: str,
        pixel_to_micron: float
    ):
        """
        Run the export operation.
        
        Args:
            input_path: Path to input HDF5 file
            output_dir: Output directory
            format_type: "csv", "images", or "all"
            frame_type: "valid", "invalid", or "both"
            pixel_to_micron: Pixel to micron conversion factor
        """
        self._cancel_requested = False
        
        try:
            # Validate input file
            if not input_path.exists():
                self.finished.emit(False, f"Input file does not exist: {input_path}")
                return
            
            if not input_path.is_file():
                self.finished.emit(False, f"Input path is not a file: {input_path}")
                return
            
            # Create output directory
            try:
                output_dir.mkdir(parents=True, exist_ok=True)
            except OSError as e:
                self.finished.emit(False, f"Failed to create output directory: {e}")
                return
            
            # Check if cv2 is needed
            if format_type in ("images", "all") and not HAS_CV2:
                self.finished.emit(False, "opencv-python (cv2) is required for image export.")
                return
            
            self.progress_message.emit("Opening HDF5 file...")
            self.progress_value.emit(10)
            
            # Open HDF5 file
            with h5py.File(input_path, 'r') as h5_file:
                if self._cancel_requested:
                    self.finished.emit(False, "Export cancelled by user")
                    return
                
                # Read metadata
                self.progress_message.emit("Reading metadata...")
                self.progress_value.emit(20)
                
                metadata_valid = None
                metadata_invalid = None
                
                if frame_type in ("valid", "both"):
                    metadata_valid = read_hdf5_metadata(h5_file, "/valid_frames/metadata")
                    if metadata_valid is None:
                        self.progress_message.emit("WARNING: /valid_frames/metadata dataset not found")
                
                if frame_type in ("invalid", "both"):
                    metadata_invalid = read_hdf5_metadata(h5_file, "/invalid_frames/metadata")
                    if metadata_invalid is None:
                        self.progress_message.emit("WARNING: /invalid_frames/metadata dataset not found")
                
                if self._cancel_requested:
                    self.finished.emit(False, "Export cancelled by user")
                    return
                
                # Check if we have any data
                if metadata_valid is None and metadata_invalid is None:
                    self.finished.emit(False, "No metadata found in HDF5 file")
                    return
                
                # Export CSV if requested
                if format_type in ("csv", "all"):
                    self.progress_message.emit("Exporting metrics to CSV...")
                    self.progress_value.emit(30)
                    
                    csv_path = output_dir / "metrics.csv"
                    valid_count, invalid_count = export_metrics_to_csv(
                        metadata_valid,
                        metadata_invalid,
                        csv_path,
                        pixel_to_micron,
                        frame_type
                    )
                    total_count = valid_count + invalid_count
                    self.progress_message.emit(
                        f"Exported {total_count} frames to CSV (Valid: {valid_count}, Invalid: {invalid_count})"
                    )
                    
                    if self._cancel_requested:
                        self.finished.emit(False, "Export cancelled by user")
                        return
                
                # Export images if requested
                if format_type in ("images", "all"):
                    self.progress_message.emit("Exporting images...")
                    self.progress_value.emit(50)

                    total_exported = 0
                    total_failed = 0
                    expected_any_images = False

                    # Export valid frames
                    if frame_type in ("valid", "both") and metadata_valid is not None:
                        images_valid = read_hdf5_images(h5_file, "/valid_frames/images")
                        if images_valid is not None:
                            expected_any_images = expected_any_images or len(images_valid) > 0
                            self.progress_message.emit("Exporting valid frame images...")
                            exported, failed = export_images_to_tiff(
                                images_valid,
                                metadata_valid,
                                output_dir,
                                "valid"
                            )
                            total_exported += exported
                            total_failed += failed
                            self.progress_message.emit(f"Exported {exported} valid frame images")
                            if failed > 0:
                                self.progress_message.emit(
                                    f"WARNING: {failed} valid frame images failed to write"
                                )
                        else:
                            self.progress_message.emit("WARNING: /valid_frames/images dataset not found")

                        if self._cancel_requested:
                            self.finished.emit(False, "Export cancelled by user")
                            return

                    # Export invalid frames
                    if frame_type in ("invalid", "both") and metadata_invalid is not None:
                        images_invalid = read_hdf5_images(h5_file, "/invalid_frames/images")
                        if images_invalid is not None:
                            expected_any_images = expected_any_images or len(images_invalid) > 0
                            self.progress_message.emit("Exporting invalid frame images...")
                            exported, failed = export_images_to_tiff(
                                images_invalid,
                                metadata_invalid,
                                output_dir,
                                "invalid"
                            )
                            total_exported += exported
                            total_failed += failed
                            self.progress_message.emit(f"Exported {exported} invalid frame images")
                            if failed > 0:
                                self.progress_message.emit(
                                    f"WARNING: {failed} invalid frame images failed to write"
                                )
                        else:
                            self.progress_message.emit("WARNING: /invalid_frames/images dataset not found")

                        if self._cancel_requested:
                            self.finished.emit(False, "Export cancelled by user")
                            return

                    self.progress_message.emit(f"Total images exported: {total_exported}")

                    # Refuse to claim success if images were requested but nothing landed.
                    if expected_any_images and total_exported == 0:
                        self.finished.emit(
                            False,
                            "No images were written. The output path may contain "
                            "characters unsupported by the OS image writer (e.g. "
                            "non-ASCII characters on Windows). Try an output path "
                            "with only ASCII characters."
                        )
                        return
                
                # Read experiment info
                exp_info = read_experiment_info(h5_file)
                if exp_info:
                    info_text = "\nExperiment Info:\n"
                    for key, value in sorted(exp_info.items()):
                        info_text += f"  {key}: {value}\n"
                    self.progress_message.emit(info_text)
                
                self.progress_value.emit(100)
                self.finished.emit(True, f"Export complete. Output directory: {output_dir}")
        
        except IOError as e:
            self.finished.emit(False, f"Failed to open HDF5 file: {e}")
        except Exception as e:
            self.finished.emit(False, f"Unexpected error: {e}")
