import { useState } from "react";
import { ScatterPlot } from "../charts/ScatterPlot";
import { Histogram } from "../charts/Histogram";
import { FrameGrid } from "../common/FrameGrid";
import { MetricsTable } from "../common/MetricsTable";
import type { ProcessedFrame, HdfOverlayMode } from "../../types/backend";
import { Tabs, TabsList, TabsTrigger, TabsContent } from "../ui/tabs";
import { Button } from "../ui/button";
import { Checkbox } from "../ui/checkbox";
import { Label } from "../ui/label";

const OVERLAY_MODES: { value: HdfOverlayMode; label: string }[] = [
  { value: "none", label: "None" },
  { value: "all_contours", label: "All contours" },
  { value: "outer_inner_color", label: "Outer+inner" },
  { value: "all_mask", label: "All mask" },
  { value: "filtered_mask", label: "Filtered mask" },
];

export function ReviewTab() {
  const [overlayMode, setOverlayMode] = useState<HdfOverlayMode>("none");
  const [showRoi, setShowRoi] = useState(false);
  const [filePath] = useState("No file selected");
  const [status, setStatus] = useState("Ready");
  const [validFrames] = useState<ProcessedFrame[]>([]);
  const [invalidFrames] = useState<ProcessedFrame[]>([]);
  const [selectedFrameIndex, setSelectedFrameIndex] = useState<number | null>(null);
  const fileLoaded = validFrames.length > 0 || invalidFrames.length > 0;

  const handleSelectFile = async () => {
    // TODO: Use Tauri dialog to pick HDF5 file, then load frames
    setStatus("Loading...");
  };

  const scatterData = validFrames.map((f) => ({
    area: f.validation.area,
    deformability: f.validation.deformability,
    isTargetGroup: f.validation.isTargetGroup,
  }));
  const histogramData = validFrames.map((f) => f.validation.ringRatio);

  return (
    <div className="flex flex-col h-full p-1.5 gap-1.5">
      {/* Controls row */}
      <div className="flex items-center gap-2 flex-shrink-0 flex-wrap">
        <Button size="sm" onClick={handleSelectFile}>Select HDF File...</Button>
        <Button size="sm" variant="outline" disabled={!fileLoaded}>Export Metrics to CSV...</Button>
        <Button size="sm" variant="outline" disabled={!fileLoaded}>Export All...</Button>
        <Button size="sm" variant="outline" disabled={!fileLoaded}>Export Charts as TIFF...</Button>

        <span className="text-xs text-muted-foreground">Overlay:</span>
        <select
          className="select-styled"
          value={overlayMode}
          onChange={(e) => setOverlayMode(e.target.value as HdfOverlayMode)}
          disabled={!fileLoaded}
          style={{ width: 140 }}
        >
          {OVERLAY_MODES.map((m) => (
            <option key={m.value} value={m.value}>{m.label}</option>
          ))}
        </select>

        <span className="overlay-legend flex items-center gap-1">
          <span className="swatch" style={{ backgroundColor: "var(--color-target)" }} /> Target{" "}
          <span className="swatch" style={{ backgroundColor: "var(--color-valid)" }} /> Valid{" "}
          <span className="swatch" style={{ backgroundColor: "var(--color-invalid)" }} /> Invalid
        </span>

        <div className="flex items-center gap-1.5">
          <Checkbox
            id="show-roi"
            checked={showRoi}
            onCheckedChange={(v) => setShowRoi(v === true)}
            disabled={!fileLoaded}
          />
          <Label htmlFor="show-roi" className="text-xs">Show ROI</Label>
        </div>

        <span className="flex-1 text-xs text-muted-foreground truncate">{filePath}</span>
        <span className="text-xs text-muted-foreground flex-shrink-0">{status}</span>
      </div>

      {/* Frame tabs */}
      <Tabs defaultValue="valid" className="flex-1 flex flex-col min-h-0">
        <TabsList>
          <TabsTrigger value="valid">Valid Frames</TabsTrigger>
          <TabsTrigger value="invalid">Invalid Frames</TabsTrigger>
          <TabsTrigger value="charts">Charts</TabsTrigger>
        </TabsList>

        <TabsContent value="valid" className="border border-border rounded-b-md bg-background">
          <FrameMetricsView
            frames={validFrames}
            overlayMode={overlayMode}
            selectedIndex={selectedFrameIndex}
            onSelect={setSelectedFrameIndex}
          />
        </TabsContent>

        <TabsContent value="invalid" className="border border-border rounded-b-md bg-background">
          <FrameMetricsView
            frames={invalidFrames}
            overlayMode={overlayMode}
            selectedIndex={selectedFrameIndex}
            onSelect={setSelectedFrameIndex}
          />
        </TabsContent>

        <TabsContent value="charts" className="border border-border rounded-b-md bg-background">
          <div className="flex h-full gap-1.5 p-1.5">
            <div className="flex-1 min-w-0 min-h-0">
              <ScatterPlot data={scatterData} />
            </div>
            <div className="flex-1 min-w-0 min-h-0">
              <Histogram data={histogramData} />
            </div>
          </div>
        </TabsContent>
      </Tabs>
    </div>
  );
}

function FrameMetricsView({ frames, overlayMode, selectedIndex, onSelect }: {
  frames: ProcessedFrame[];
  overlayMode: HdfOverlayMode;
  selectedIndex: number | null;
  onSelect: (index: number) => void;
}) {
  return (
    <div className="flex h-full">
      <div className="min-w-[400px] overflow-auto border-r border-border">
        <FrameGrid
          frames={frames}
          showOverlay={overlayMode !== "none"}
          selectedIndex={selectedIndex}
          onSelect={onSelect}
        />
      </div>
      <div className="flex-1 min-w-0 overflow-auto">
        <MetricsTable
          frames={frames}
          selectedIndex={selectedIndex}
          onSelect={onSelect}
        />
      </div>
    </div>
  );
}
