import { useState } from "react";
import { ResizableSplitter } from "../layout/ResizableSplitter";
import { ImageCanvas } from "../canvas/ImageCanvas";
import { usePlaybackStore } from "../../stores/playbackStore";
import { Button } from "../ui/button";
import { Textarea } from "../ui/textarea";

export function OverviewTab() {
  const zoomMode = usePlaybackStore((s) => s.zoomMode);
  const toggleZoom = usePlaybackStore((s) => s.toggleZoomMode);
  const [roiOverlay, setRoiOverlay] = useState(false);
  const [jsContent, setJsContent] = useState("");
  const [jsPath] = useState("");
  const [unsaved, setUnsaved] = useState(false);

  return (
    <div className="h-full">
      <ResizableSplitter direction="vertical" defaultSizes={[60, 40]} minSizes={[100, 0]}>
        {/* Canvas area with controls */}
        <div className="flex flex-col h-full">
          <div className="flex items-center px-2 py-1 gap-2 flex-shrink-0" style={{ maxHeight: 40 }}>
            <Button variant="ghost" size="sm" onClick={toggleZoom}>
              Fit: {zoomMode === "fit" ? "Window" : "100%"}
            </Button>
            <Button variant="ghost" size="sm" onClick={() => setRoiOverlay(!roiOverlay)}>
              ROI Overlay: {roiOverlay ? "On" : "Off"}
            </Button>
            <div className="flex-1" />
          </div>
          <div className="flex-1 min-h-0">
            <ImageCanvas />
          </div>
        </div>

        {/* JS Editor area */}
        <div className="flex flex-col h-full p-1.5 gap-1.5">
          <div className="flex items-center gap-2 flex-shrink-0 flex-wrap">
            <Button size="sm" variant="outline">Reset</Button>
            <Button size="sm" onClick={() => setUnsaved(false)}>Save</Button>
            <Button size="sm" variant="outline">Apply to Camera</Button>
            <Button size="sm" variant="outline">Browse...</Button>
            <Button size="sm" variant="outline">Clear</Button>
            <div className="flex-1" />
            <span className="text-xs text-muted-foreground truncate max-w-[400px]">
              {jsPath || "No file loaded"}
            </span>
            {unsaved && (
              <span className="text-xs text-amber-500">
                Unsaved changes - click Save to apply.
              </span>
            )}
          </div>
          <Textarea
            value={jsContent}
            onChange={(e) => {
              setJsContent(e.target.value);
              setUnsaved(true);
            }}
            className="flex-1 min-h-0 resize-none font-mono text-xs"
            style={{ whiteSpace: "pre", overflowWrap: "normal", overflowX: "auto" }}
            spellCheck={false}
          />
        </div>
      </ResizableSplitter>
    </div>
  );
}
