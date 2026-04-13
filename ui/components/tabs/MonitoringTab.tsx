import { useState } from "react";
import { ScatterPlot } from "../charts/ScatterPlot";
import { Histogram } from "../charts/Histogram";
import { FrameGrid } from "../common/FrameGrid";
import { TuneParamsPanel } from "../panels/TuneParamsPanel";
import { useProcessingStore } from "../../stores/processingStore";
import { clearMonitoringFrames, fireSortTrigger, setTriggerDuration } from "../../hooks/useBackend";
import { Button } from "../ui/button";
import { Input } from "../ui/input";
import { Checkbox } from "../ui/checkbox";
import { Label } from "../ui/label";

export function MonitoringTab() {
  const [triggerDurationUs, setTriggerDurationUs] = useState(1);
  const [validOverlay, setValidOverlay] = useState(false);
  const [invalidOverlay, setInvalidOverlay] = useState(false);

  const monitoringValid = useProcessingStore((s) => s.monitoringValidFrames);
  const monitoringInvalid = useProcessingStore((s) => s.monitoringInvalidFrames);

  const handleClearBuffer = async () => {
    try {
      await clearMonitoringFrames();
      useProcessingStore.getState().setMonitoringValidFrames([]);
      useProcessingStore.getState().setMonitoringInvalidFrames([]);
    } catch (e) {
      console.error("Failed to clear buffer:", e);
    }
  };

  const handleSortTrigger = async () => {
    try {
      await fireSortTrigger();
    } catch (e) {
      console.error("Failed to fire trigger:", e);
    }
  };

  const handleTriggerDurationChange = async (value: number) => {
    setTriggerDurationUs(value);
    try {
      await setTriggerDuration(value);
    } catch {
      // Ignore
    }
  };

  const scatterData = monitoringValid.map((f) => ({
    area: f.validation.area,
    deformability: f.validation.deformability,
    isTargetGroup: f.validation.isTargetGroup,
  }));

  const histogramData = monitoringValid.map((f) => f.validation.ringRatio);

  return (
    <div
      className="h-full min-h-0"
      style={{
        display: "grid",
        gridTemplateColumns: "1fr 1fr auto",
        gridTemplateRows: "auto 1fr 1fr",
        gap: 6,
        padding: 6,
      }}
    >
      {/* Row 0, col 0-1: Controls */}
      <div
        className="flex items-center gap-2"
        style={{ gridColumn: "1 / 3", gridRow: 1 }}
      >
        <Button size="sm" variant="outline" onClick={handleClearBuffer}>
          Clear Buffer
        </Button>
        <Button
          size="sm"
          variant="outline"
          onClick={handleSortTrigger}
          title="Manually fire a sort trigger pulse for testing"
        >
          Sort Trigger
        </Button>
        <Input
          type="number"
          min={1}
          max={1000000}
          value={triggerDurationUs}
          onChange={(e) => handleTriggerDurationChange(Number(e.target.value))}
          className="w-28"
          title="Trigger pulse duration in microseconds"
        />
        <span className="text-xs text-muted-foreground">us</span>
        <div className="flex-1" />
      </div>

      {/* Row 1, col 0: Scatter plot */}
      <div style={{ gridColumn: 1, gridRow: 2, minHeight: 0, minWidth: 0 }}>
        <ScatterPlot data={scatterData} />
      </div>

      {/* Row 1, col 1: Histogram */}
      <div style={{ gridColumn: 2, gridRow: 2, minHeight: 0, minWidth: 0 }}>
        <Histogram data={histogramData} />
      </div>

      {/* Row 2, col 0: Valid frames */}
      <div
        className="flex flex-col gap-1 min-h-0"
        style={{ gridColumn: 1, gridRow: 3 }}
      >
        <div className="flex items-center gap-2 flex-shrink-0">
          <Checkbox
            id="valid-overlay"
            checked={validOverlay}
            onCheckedChange={(v) => setValidOverlay(v === true)}
          />
          <Label htmlFor="valid-overlay" className="text-xs">Show Overlay</Label>
          <div className="flex-1" />
        </div>
        <div className="flex-1 min-h-[200px] overflow-auto rounded-md border border-border bg-background">
          <FrameGrid frames={monitoringValid} showOverlay={validOverlay} />
        </div>
      </div>

      {/* Row 2, col 1: Invalid frames */}
      <div
        className="flex flex-col gap-1 min-h-0"
        style={{ gridColumn: 2, gridRow: 3 }}
      >
        <div className="flex items-center gap-2 flex-shrink-0">
          <Checkbox
            id="invalid-overlay"
            checked={invalidOverlay}
            onCheckedChange={(v) => setInvalidOverlay(v === true)}
          />
          <Label htmlFor="invalid-overlay" className="text-xs">Show Overlay</Label>
          <div className="flex-1" />
        </div>
        <div className="flex-1 min-h-[200px] overflow-auto rounded-md border border-border bg-background">
          <FrameGrid frames={monitoringInvalid} showOverlay={invalidOverlay} />
        </div>
      </div>

      {/* Row 0-2, col 2: Tune params */}
      <div style={{ gridColumn: 3, gridRow: "1 / 4", minWidth: 0, minHeight: 0 }}>
        <TuneParamsPanel />
      </div>
    </div>
  );
}
