import { useState } from "react";
import { ScatterPlot } from "../charts/ScatterPlot";
import { Histogram } from "../charts/Histogram";
import { FrameGrid } from "../common/FrameGrid";
import { MetricsTable } from "../common/MetricsTable";
import type { ProcessedFrame, HdfOverlayMode } from "../../types/backend";

const OVERLAY_MODES: { value: HdfOverlayMode; label: string }[] = [
  { value: "none", label: "None" },
  { value: "all_contours", label: "All contours" },
  { value: "outer_inner_color", label: "Outer+inner" },
  { value: "all_mask", label: "All mask" },
  { value: "filtered_mask", label: "Filtered mask" },
];

export function ReviewTab() {
  const [frameTab, setFrameTab] = useState<"valid" | "invalid" | "charts">("valid");
  const [overlayMode, setOverlayMode] = useState<HdfOverlayMode>("none");
  const [showRoi, setShowRoi] = useState(false);
  const [filePath, setFilePath] = useState("No file selected");
  const [status, setStatus] = useState("Ready");
  const [validFrames, setValidFrames] = useState<ProcessedFrame[]>([]);
  const [invalidFrames, setInvalidFrames] = useState<ProcessedFrame[]>([]);
  const [selectedFrameIndex, setSelectedFrameIndex] = useState<number | null>(null);
  const fileLoaded = validFrames.length > 0 || invalidFrames.length > 0;

  const handleSelectFile = async () => {
    // TODO: Use Tauri dialog to pick HDF5 file, then load frames
    setStatus("Loading...");
  };

  const currentFrames = frameTab === "valid" ? validFrames : invalidFrames;
  const scatterData = validFrames.map((f) => ({
    area: f.validation.area,
    deformability: f.validation.deformability,
    isTargetGroup: f.validation.isTargetGroup,
  }));
  const histogramData = validFrames.map((f) => f.validation.ringRatio);

  return (
    /* QVBoxLayout margins 6, spacing 6 */
    <div style={{ display: "flex", flexDirection: "column", height: "100%", padding: 6, gap: 6 }}>
      {/* Row 1 (QHBoxLayout): buttons, overlay controls, file path, status */}
      <div style={{ display: "flex", alignItems: "center", gap: 6, flexShrink: 0, flexWrap: "wrap" }}>
        <button className="qt-btn" onClick={handleSelectFile}>
          Select HDF File...
        </button>
        <button className="qt-btn" disabled={!fileLoaded}>
          Export Metrics to CSV...
        </button>
        <button className="qt-btn" disabled={!fileLoaded}>
          Export All...
        </button>
        <button className="qt-btn" disabled={!fileLoaded}>
          Export Charts as TIFF...
        </button>

        {/* "Overlay:" label */}
        <span style={{ fontSize: 12 }}>Overlay:</span>

        {/* QComboBox */}
        <select
          className="qt-select"
          value={overlayMode}
          onChange={(e) => setOverlayMode(e.target.value as HdfOverlayMode)}
          disabled={!fileLoaded}
        >
          {OVERLAY_MODES.map((m) => (
            <option key={m.value} value={m.value}>
              {m.label}
            </option>
          ))}
        </select>

        {/* Overlay legend */}
        <span className="overlay-legend">
          <span className="swatch" style={{ backgroundColor: "var(--color-target)" }} /> Target{" "}
          <span className="swatch" style={{ backgroundColor: "var(--color-valid)" }} /> Valid{" "}
          <span className="swatch" style={{ backgroundColor: "var(--color-invalid)" }} /> Invalid
        </span>

        {/* Show ROI checkbox */}
        <label className="qt-checkbox">
          <input
            type="checkbox"
            checked={showRoi}
            onChange={(e) => setShowRoi(e.target.checked)}
            disabled={!fileLoaded}
          />
          Show ROI
        </label>

        {/* File path label (Expanding, stretch 1) */}
        <span
          style={{
            flex: 1,
            fontSize: 12,
            color: "#999",
            overflow: "hidden",
            textOverflow: "ellipsis",
            whiteSpace: "nowrap",
          }}
        >
          {filePath}
        </span>

        {/* Status label */}
        <span style={{ fontSize: 12, color: "#666", flexShrink: 0 }}>{status}</span>
      </div>

      {/* QTabWidget with 3 tabs: "Valid Frames", "Invalid Frames", "Charts" */}
      <div style={{ flex: 1, display: "flex", flexDirection: "column", minHeight: 0 }}>
        {/* Tab bar */}
        <div className="qt-tab-bar">
          <button
            className="qt-tab"
            data-active={frameTab === "valid"}
            onClick={() => setFrameTab("valid")}
          >
            Valid Frames
          </button>
          <button
            className="qt-tab"
            data-active={frameTab === "invalid"}
            onClick={() => setFrameTab("invalid")}
          >
            Invalid Frames
          </button>
          <button
            className="qt-tab"
            data-active={frameTab === "charts"}
            onClick={() => setFrameTab("charts")}
          >
            Charts
          </button>
        </div>

        {/* Tab content */}
        <div
          style={{
            flex: 1,
            minHeight: 0,
            border: "1px solid #c0c0c0",
            borderTop: "none",
            background: "white",
          }}
        >
          {/* Valid/Invalid: QHBoxLayout margins 0 - QScrollArea (min 400px, grid spacing 4) | QTableView (select rows, single selection) */}
          {(frameTab === "valid" || frameTab === "invalid") && (
            <div style={{ display: "flex", height: "100%", margin: 0 }}>
              {/* QScrollArea (min 400px, grid spacing 4) */}
              <div
                style={{
                  minWidth: 400,
                  overflow: "auto",
                  borderRight: "1px solid #c0c0c0",
                }}
              >
                <FrameGrid
                  frames={currentFrames}
                  showOverlay={overlayMode !== "none"}
                  selectedIndex={selectedFrameIndex}
                  onSelect={setSelectedFrameIndex}
                />
              </div>

              {/* QTableView (select rows, single selection) */}
              <div style={{ flex: 1, minWidth: 0, overflow: "auto" }}>
                <MetricsTable
                  frames={currentFrames}
                  selectedIndex={selectedFrameIndex}
                  onSelect={setSelectedFrameIndex}
                />
              </div>
            </div>
          )}

          {/* Charts: QHBoxLayout - scatter plot (Expanding) | histogram (Expanding) */}
          {frameTab === "charts" && (
            <div style={{ display: "flex", height: "100%", gap: 6, padding: 6 }}>
              <div style={{ flex: 1, minWidth: 0, minHeight: 0 }}>
                <ScatterPlot data={scatterData} />
              </div>
              <div style={{ flex: 1, minWidth: 0, minHeight: 0 }}>
                <Histogram data={histogramData} />
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
