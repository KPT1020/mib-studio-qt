import { useState } from "react";
import { ResizableSplitter } from "../layout/ResizableSplitter";
import { ImageCanvas } from "../canvas/ImageCanvas";
import { usePlaybackStore } from "../../stores/playbackStore";

export function OverviewTab() {
  const zoomMode = usePlaybackStore((s) => s.zoomMode);
  const toggleZoom = usePlaybackStore((s) => s.toggleZoomMode);
  const [roiOverlay, setRoiOverlay] = useState(false);
  const [jsContent, setJsContent] = useState("");
  const [jsPath, setJsPath] = useState("");
  const [unsaved, setUnsaved] = useState(false);

  return (
    <ResizableSplitter direction="vertical" defaultSizes={[60, 40]} minSizes={[100, 0]}>
      {/* Top: Canvas with controls */}
      <div className="flex flex-col h-full">
        {/* Controls bar - max height 40px */}
        <div
          className="flex items-center gap-1.5 px-1.5 py-1 flex-shrink-0"
          style={{ maxHeight: "var(--control-bar-height)" }}
        >
          <button
            onClick={toggleZoom}
            className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
          >
            Fit: {zoomMode === "fit" ? "Window" : "100%"}
          </button>
          <button
            onClick={() => setRoiOverlay(!roiOverlay)}
            className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
          >
            ROI Overlay: {roiOverlay ? "On" : "Off"}
          </button>
          <div className="flex-1" />
        </div>

        {/* Canvas */}
        <div className="flex-1 min-h-0">
          <ImageCanvas />
        </div>
      </div>

      {/* Bottom: JS Editor */}
      <div className="flex flex-col h-full p-1.5" style={{ gap: "var(--spacing-sm)" }}>
        {/* Button row */}
        <div className="flex items-center gap-1.5 flex-shrink-0">
          <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
            Reset
          </button>
          <button
            className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
            onClick={() => setUnsaved(false)}
          >
            Save
          </button>
          <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
            Apply to Camera
          </button>
          <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
            Browse...
          </button>
          <button className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
            Clear
          </button>
          <div className="flex-1" />
          <span className="text-xs text-neutral-500 truncate max-w-[400px]">
            {jsPath || "No file loaded"}
          </span>
          <div style={{ width: "8px" }} />
          {unsaved && (
            <span className="text-xs" style={{ color: "var(--color-unsaved)" }}>
              Unsaved changes - click Save to apply.
            </span>
          )}
        </div>

        {/* Editor area */}
        <div className="flex-1 min-h-0">
          <textarea
            value={jsContent}
            onChange={(e) => {
              setJsContent(e.target.value);
              setUnsaved(true);
            }}
            className="w-full h-full font-mono text-xs p-2 border border-neutral-300 resize-none bg-white"
            style={{ whiteSpace: "pre", overflowWrap: "normal", overflowX: "auto" }}
            spellCheck={false}
          />
        </div>
      </div>
    </ResizableSplitter>
  );
}
