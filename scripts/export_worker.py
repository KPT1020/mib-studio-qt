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
    export_series_images_to_tiff,
    ensure_output_root,
    resolve_export_targets,
    HAS_CV2,
    HAS_HDF5_DEPS,
    HDF5_IMPORT_ERROR,
    h5py,
)


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
            
            ok, error = ensure_output_root(output_dir)
            if not ok:
                self.finished.emit(False, error)
                return

            if not HAS_HDF5_DEPS:
                self.finished.emit(
                    False,
                    "Required dependencies not installed. Install with: pip install h5py numpy. "
                    f"Details: {HDF5_IMPORT_ERROR}"
                )
                return

            csv_path, data_output_dir = resolve_export_targets(input_path, output_dir, format_type)

            # Check if cv2 is needed
            if format_type in ("images", "all") and not HAS_CV2:
                self.finished.emit(False, "opencv-python (cv2) is required for image export.")
                return

            if format_type in ("images", "all"):
                try:
                    data_output_dir.mkdir(parents=True, exist_ok=False)
                except OSError as e:
                    self.finished.emit(False, f"Failed to create export directory: {e}")
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
                    
                    if csv_path is None:
                        self.finished.emit(False, "CSV output path was not resolved")
                        return
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
                    
                    # Export valid frames
                    if frame_type in ("valid", "both") and metadata_valid is not None:
                        images_valid = read_hdf5_images(h5_file, "/valid_frames/images")
                        if images_valid is not None:
                            self.progress_message.emit("Exporting valid frame images...")
                            exported = export_images_to_tiff(
                                images_valid,
                                metadata_valid,
                                data_output_dir,
                                "valid"
                            )
                            total_exported += exported
                            self.progress_message.emit(f"Exported {exported} valid frame images")
                        else:
                            self.progress_message.emit("WARNING: /valid_frames/images dataset not found")

                        series_exported = export_series_images_to_tiff(
                            h5_file, metadata_valid, data_output_dir
                        )
                        if series_exported > 0:
                            total_exported += series_exported
                            self.progress_message.emit(f"Exported {series_exported} series images")
                        
                        if self._cancel_requested:
                            self.finished.emit(False, "Export cancelled by user")
                            return
                    
                    # Export invalid frames
                    if frame_type in ("invalid", "both") and metadata_invalid is not None:
                        images_invalid = read_hdf5_images(h5_file, "/invalid_frames/images")
                        if images_invalid is not None:
                            self.progress_message.emit("Exporting invalid frame images...")
                            exported = export_images_to_tiff(
                                images_invalid,
                                metadata_invalid,
                                data_output_dir,
                                "invalid"
                            )
                            total_exported += exported
                            self.progress_message.emit(f"Exported {exported} invalid frame images")
                        else:
                            self.progress_message.emit("WARNING: /invalid_frames/images dataset not found")
                        
                        if self._cancel_requested:
                            self.finished.emit(False, "Export cancelled by user")
                            return
                    
                    self.progress_message.emit(f"Total images exported: {total_exported}")
                
                # Read experiment info
                exp_info = read_experiment_info(h5_file)
                if exp_info:
                    info_text = "\nExperiment Info:\n"
                    for key, value in sorted(exp_info.items()):
                        info_text += f"  {key}: {value}\n"
                    self.progress_message.emit(info_text)
                
                self.progress_value.emit(100)
                final_output = data_output_dir if format_type in ("images", "all") else output_dir
                self.finished.emit(True, f"Export complete. Output directory: {final_output}")
        
        except IOError as e:
            self.finished.emit(False, f"Failed to open HDF5 file: {e}")
        except Exception as e:
            self.finished.emit(False, f"Unexpected error: {e}")
