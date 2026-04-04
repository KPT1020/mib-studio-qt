import { useState } from "react";
import { ScatterPlot } from "../charts/ScatterPlot";
import { Histogram } from "../charts/Histogram";
import { FrameGrid } from "../common/FrameGrid";
import { MetricsTable } from "../common/MetricsTable";
import type { ProcessedFrame, HdfOverlayMode } from "../../types/backend";

const OVERLAY_MODES: { value: HdfOverlayMode; label: string }[] = [
  { value: "none", label: "None" },
  { value: "all_contours", label: "All contours" },
  { value: "outer_inner_color", label: "Outer+inner (color)" },
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
    <div className="flex flex-col h-full" style={{ padding: "var(--spacing-sm)", gap: "var(--spacing-sm)" }}>
      {/* Top row: file controls */}
      <div className="flex items-center gap-2 flex-shrink-0 flex-wrap">
        <button
          onClick={handleSelectFile}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          Select HDF File...
        </button>
        <button
          disabled={!fileLoaded}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
        >
          Export Metrics to CSV...
        </button>
        <button
          disabled={!fileLoaded}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
        >
          Export All...
        </button>
        <button
          disabled={!fileLoaded}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
        >
          Export Charts as TIFF...
        </button>

        <span className="text-xs text-neutral-600">Overlay:</span>
        <select
          value={overlayMode}
          onChange={(e) => setOverlayMode(e.target.value as HdfOverlayMode)}
          disabled={!fileLoaded}
          className="text-xs border border-neutral-400 rounded px-1 py-0.5"
        >
          {OVERLAY_MODES.map((m) => (
            <option key={m.value} value={m.value}>
              {m.label}
            </option>
          ))}
        </select>

        {/* Overlay legend */}
        <span className="text-xs">
          <span style={{ color: "var(--color-target)" }}>&#9632;</span> Target{" "}
          <span style={{ color: "var(--color-valid)" }}>&#9632;</span> Valid{" "}
          <span style={{ color: "var(--color-invalid)" }}>&#9632;</span> Invalid
        </span>

        <label className="flex items-center gap-1 text-xs">
          <input
            type="checkbox"
            checked={showRoi}
            onChange={(e) => setShowRoi(e.target.checked)}
            disabled={!fileLoaded}
          />
          Show ROI
        </label>

        <span className="flex-1 text-xs text-neutral-500 truncate">{filePath}</span>
        <span className="text-xs text-neutral-600">{status}</span>
      </div>

      {/* Frame type tabs */}
      <div className="flex-1 flex flex-col min-h-0">
        {/* Tab headers */}
        <div className="flex border-b border-neutral-300 flex-shrink-0">
          {(["valid", "invalid", "charts"] as const).map((tab) => (
            <button
              key={tab}
              onClick={() => setFrameTab(tab)}
              className={`px-4 py-1.5 text-xs border-r border-neutral-300 capitalize ${
                frameTab === tab
                  ? "bg-white font-semibold border-b-2 border-b-blue-500"
                  : "bg-neutral-100 hover:bg-neutral-200 cursor-pointer"
              }`}
            >
              {tab === "valid" ? "Valid Frames" : tab === "invalid" ? "Invalid Frames" : "Charts"}
            </button>
          ))}
        </div>

        {/* Tab content */}
        <div className="flex-1 min-h-0">
          {(frameTab === "valid" || frameTab === "invalid") && (
            <div className="flex h-full">
              {/* Image grid - min 400px */}
              <div
                className="overflow-y-auto border-r border-neutral-300 bg-white"
                style={{ minWidth: 400 }}
              >
                <FrameGrid
                  frames={currentFrames}
                  showOverlay={overlayMode !== "none"}
                  selectedIndex={selectedFrameIndex}
                  onSelect={setSelectedFrameIndex}
                />
              </div>

              {/* Metrics table */}
              <div className="flex-1 min-w-0">
                <MetricsTable
                  frames={currentFrames}
                  selectedIndex={selectedFrameIndex}
                  onSelect={setSelectedFrameIndex}
                />
              </div>
            </div>
          )}

          {frameTab === "charts" && (
            <div className="flex h-full gap-2 p-2">
              <div className="flex-1 min-w-0">
                <ScatterPlot data={scatterData} />
              </div>
              <div className="flex-1 min-w-0">
                <Histogram data={histogramData} />
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
