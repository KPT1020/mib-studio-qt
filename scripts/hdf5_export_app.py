#!/usr/bin/env python3
"""
HDF5 Export GUI Application

A PySide6-based GUI application for exporting metrics and images from MIB Studio HDF5 files.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Optional

try:
    from PySide6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
        QPushButton, QLabel, QLineEdit, QComboBox, QDoubleSpinBox,
        QProgressBar, QTextEdit, QFileDialog, QMessageBox, QGroupBox
    )
    from PySide6.QtCore import Qt, QThread, Signal
except ImportError as e:
    print("ERROR: PySide6 is required for the GUI application.", file=sys.stderr)
    print("Install with: pip install PySide6", file=sys.stderr)
    print(f"Details: {e}", file=sys.stderr)
    sys.exit(1)

from export_worker import ExportWorker


class ExportWindow(QMainWindow):
    """Main window for the HDF5 export application."""
    
    def __init__(self):
        super().__init__()
        self.worker_thread: Optional[QThread] = None
        self.worker: Optional[ExportWorker] = None
        self.init_ui()
    
    def init_ui(self):
        """Initialize the user interface."""
        self.setWindowTitle("HDF5 Export Tool")
        self.setMinimumWidth(600)
        self.setMinimumHeight(500)
        
        # Central widget
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # Main layout
        layout = QVBoxLayout(central_widget)
        layout.setSpacing(10)
        layout.setContentsMargins(15, 15, 15, 15)
        
        # Input file group
        input_group = QGroupBox("Input File")
        input_layout = QVBoxLayout()
        
        input_file_layout = QHBoxLayout()
        self.input_file_edit = QLineEdit()
        self.input_file_edit.setPlaceholderText("Select HDF5 file...")
        input_file_btn = QPushButton("Browse...")
        input_file_btn.clicked.connect(self.browse_input_file)
        input_file_layout.addWidget(self.input_file_edit)
        input_file_layout.addWidget(input_file_btn)
        input_layout.addLayout(input_file_layout)
        
        input_group.setLayout(input_layout)
        layout.addWidget(input_group)
        
        # Output directory group
        output_group = QGroupBox("Output Directory")
        output_layout = QVBoxLayout()
        
        output_dir_layout = QHBoxLayout()
        self.output_dir_edit = QLineEdit()
        self.output_dir_edit.setPlaceholderText("Select output directory...")
        output_dir_btn = QPushButton("Browse...")
        output_dir_btn.clicked.connect(self.browse_output_dir)
        output_dir_layout.addWidget(self.output_dir_edit)
        output_dir_layout.addWidget(output_dir_btn)
        output_layout.addLayout(output_dir_layout)
        
        output_group.setLayout(output_layout)
        layout.addWidget(output_group)
        
        # Options group
        options_group = QGroupBox("Export Options")
        options_layout = QVBoxLayout()
        
        # Format selection
        format_layout = QHBoxLayout()
        format_layout.addWidget(QLabel("Format:"))
        self.format_combo = QComboBox()
        self.format_combo.addItems(["CSV (metrics only)", "Images only", "All (CSV + Images)"])
        self.format_combo.setCurrentIndex(0)
        format_layout.addWidget(self.format_combo)
        format_layout.addStretch()
        options_layout.addLayout(format_layout)
        
        # Frame type selection
        frame_type_layout = QHBoxLayout()
        frame_type_layout.addWidget(QLabel("Frame Type:"))
        self.frame_type_combo = QComboBox()
        self.frame_type_combo.addItems(["Both", "Valid only", "Invalid only"])
        self.frame_type_combo.setCurrentIndex(0)
        frame_type_layout.addWidget(self.frame_type_combo)
        frame_type_layout.addStretch()
        options_layout.addLayout(frame_type_layout)
        
        # Pixel to micron conversion
        pixel_layout = QHBoxLayout()
        pixel_layout.addWidget(QLabel("Pixel to Micron:"))
        self.pixel_to_micron_spin = QDoubleSpinBox()
        self.pixel_to_micron_spin.setRange(0.0001, 1000.0)
        self.pixel_to_micron_spin.setDecimals(4)
        self.pixel_to_micron_spin.setSingleStep(0.0001)
        self.pixel_to_micron_spin.setValue(0.4886)
        pixel_layout.addWidget(self.pixel_to_micron_spin)
        pixel_layout.addStretch()
        options_layout.addLayout(pixel_layout)
        
        options_group.setLayout(options_layout)
        layout.addWidget(options_group)
        
        # Progress bar
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        layout.addWidget(self.progress_bar)
        
        # Status text
        self.status_text = QTextEdit()
        self.status_text.setReadOnly(True)
        self.status_text.setMaximumHeight(150)
        layout.addWidget(QLabel("Status:"))
        layout.addWidget(self.status_text)
        
        # Export button
        button_layout = QHBoxLayout()
        button_layout.addStretch()
        self.export_btn = QPushButton("Export")
        self.export_btn.setMinimumWidth(120)
        self.export_btn.clicked.connect(self.start_export)
        button_layout.addWidget(self.export_btn)
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.setMinimumWidth(120)
        self.cancel_btn.setEnabled(False)
        self.cancel_btn.clicked.connect(self.cancel_export)
        button_layout.addWidget(self.cancel_btn)
        button_layout.addStretch()
        layout.addLayout(button_layout)
        
        layout.addStretch()
    
    def browse_input_file(self):
        """Open file dialog to select input HDF5 file."""
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Select HDF5 File",
            "",
            "HDF5 Files (*.h5 *.hdf5);;All Files (*)"
        )
        if file_path:
            self.input_file_edit.setText(file_path)
    
    def browse_output_dir(self):
        """Open directory dialog to select output directory."""
        dir_path = QFileDialog.getExistingDirectory(
            self,
            "Select Output Directory",
            ""
        )
        if dir_path:
            self.output_dir_edit.setText(dir_path)
    
    def get_format_type(self) -> str:
        """Get format type string from combo box."""
        index = self.format_combo.currentIndex()
        if index == 0:
            return "csv"
        elif index == 1:
            return "images"
        else:
            return "all"
    
    def get_frame_type(self) -> str:
        """Get frame type string from combo box."""
        index = self.frame_type_combo.currentIndex()
        if index == 0:
            return "both"
        elif index == 1:
            return "valid"
        else:
            return "invalid"
    
    def validate_inputs(self) -> tuple[bool, str]:
        """Validate user inputs."""
        input_path = self.input_file_edit.text().strip()
        if not input_path:
            return False, "Please select an input HDF5 file."
        
        output_dir = self.output_dir_edit.text().strip()
        if not output_dir:
            return False, "Please select an output directory."
        
        return True, ""
    
    def start_export(self):
        """Start the export operation."""
        # Validate inputs
        valid, error_msg = self.validate_inputs()
        if not valid:
            QMessageBox.warning(self, "Validation Error", error_msg)
            return
        
        # Get parameters
        input_path = Path(self.input_file_edit.text().strip())
        output_dir = Path(self.output_dir_edit.text().strip())
        format_type = self.get_format_type()
        frame_type = self.get_frame_type()
        pixel_to_micron = self.pixel_to_micron_spin.value()
        
        # Disable export button, enable cancel button
        self.export_btn.setEnabled(False)
        self.cancel_btn.setEnabled(True)
        
        # Clear status
        self.status_text.clear()
        self.progress_bar.setValue(0)
        
        # Create worker thread
        self.worker_thread = QThread()
        self.worker = ExportWorker()
        self.worker.moveToThread(self.worker_thread)
        
        # Connect signals
        self.worker_thread.started.connect(
            lambda: self.worker.run_export(
                input_path, output_dir, format_type, frame_type, pixel_to_micron
            )
        )
        self.worker.progress_message.connect(self.update_status)
        self.worker.progress_value.connect(self.progress_bar.setValue)
        self.worker.finished.connect(self.export_finished)
        
        # Start thread
        self.worker_thread.start()
    
    def cancel_export(self):
        """Cancel the export operation."""
        if self.worker:
            self.worker.cancel()
        self.cancel_btn.setEnabled(False)
        self.update_status("Cancelling export...")
    
    def update_status(self, message: str):
        """Update status text."""
        self.status_text.append(message)
        # Auto-scroll to bottom
        scrollbar = self.status_text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
    
    def export_finished(self, success: bool, message: str):
        """Handle export completion."""
        # Clean up thread
        if self.worker_thread:
            self.worker_thread.quit()
            self.worker_thread.wait()
            self.worker_thread = None
            self.worker = None
        
        # Re-enable export button, disable cancel button
        self.export_btn.setEnabled(True)
        self.cancel_btn.setEnabled(False)
        
        # Show completion message
        if success:
            self.update_status(message)
            QMessageBox.information(self, "Export Complete", message)
        else:
            self.update_status(f"ERROR: {message}")
            QMessageBox.critical(self, "Export Failed", message)
        
        self.progress_bar.setValue(100 if success else 0)


def main():
    """Main entry point."""
    app = QApplication(sys.argv)
    app.setApplicationName("HDF5 Export Tool")
    
    window = ExportWindow()
    window.show()
    
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
