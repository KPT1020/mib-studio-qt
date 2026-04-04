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
    /* QVBoxLayout margins 0, spacing 0 */
    <div style={{ height: "100%", margin: 0, padding: 0 }}>
      {/* QSplitter vertical, childrenCollapsible=false, opaqueResize=true */}
      <ResizableSplitter direction="vertical" defaultSizes={[60, 40]} minSizes={[100, 0]}>
        {/* Child 1 (canvasContainer): Expanding/Expanding, minHeight=100 */}
        <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
          {/* Controls widget: maxHeight 40px, QHBoxLayout margins 6/4/6/4 spacing 6 */}
          <div
            style={{
              maxHeight: 40,
              display: "flex",
              alignItems: "center",
              padding: "4px 6px",
              gap: 6,
              flexShrink: 0,
            }}
          >
            {/* QToolButton "Fit: Window" / "Fit: 100%" */}
            <button className="qt-tool-btn" onClick={toggleZoom}>
              Fit: {zoomMode === "fit" ? "Window" : "100%"}
            </button>
            {/* QToolButton "ROI Overlay: Off" / "ROI Overlay: On" */}
            <button className="qt-tool-btn" onClick={() => setRoiOverlay(!roiOverlay)}>
              ROI Overlay: {roiOverlay ? "On" : "Off"}
            </button>
            {/* Horizontal spacer */}
            <div style={{ flex: 1 }} />
          </div>

          {/* Canvas placeholder: Expanding/Expanding with stretch=1 */}
          <div style={{ flex: 1, minHeight: 0 }}>
            <ImageCanvas />
          </div>
        </div>

        {/* Child 2 (configWidget): Expanding/Ignored, minHeight=0 */}
        <div
          style={{
            display: "flex",
            flexDirection: "column",
            height: "100%",
            padding: 6,
            gap: 6,
          }}
        >
          {/* QVBoxLayout margins 6, spacing 6 */}
          {/* Button row: Reset, Save, "Apply to Camera", "Browse...", Clear, spacer, jsPathLabel (max 400px), 8px spacer, unsaved label (orange, hidden by default) */}
          <div style={{ display: "flex", alignItems: "center", gap: 6, flexShrink: 0 }}>
            <button className="qt-btn">Reset</button>
            <button className="qt-btn" onClick={() => setUnsaved(false)}>Save</button>
            <button className="qt-btn">Apply to Camera</button>
            <button className="qt-btn">Browse...</button>
            <button className="qt-btn">Clear</button>
            <div style={{ flex: 1 }} />
            <span
              style={{
                fontSize: 12,
                color: "#999",
                overflow: "hidden",
                textOverflow: "ellipsis",
                whiteSpace: "nowrap",
                maxWidth: 400,
              }}
            >
              {jsPath || "No file loaded"}
            </span>
            <div style={{ width: 8 }} />
            {unsaved && (
              <span style={{ fontSize: 12, color: "orange" }}>
                Unsaved changes - click Save to apply.
              </span>
            )}
          </div>

          {/* QPlainTextEdit: no word wrap, fills remaining space */}
          <textarea
            value={jsContent}
            onChange={(e) => {
              setJsContent(e.target.value);
              setUnsaved(true);
            }}
            className="qt-input"
            style={{
              flex: 1,
              minHeight: 0,
              resize: "none",
              fontFamily: "monospace",
              fontSize: 12,
              whiteSpace: "pre",
              overflowWrap: "normal",
              overflowX: "auto",
              width: "100%",
            }}
            spellCheck={false}
          />
        </div>
      </ResizableSplitter>
    </div>
  );
}
