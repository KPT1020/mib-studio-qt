import { useState } from "react";
import { ScatterPlot } from "../charts/ScatterPlot";
import { Histogram } from "../charts/Histogram";
import { FrameGrid } from "../common/FrameGrid";
import { TuneParamsPanel } from "../panels/TuneParamsPanel";
import { useProcessingStore } from "../../stores/processingStore";
import { clearMonitoringFrames, fireSortTrigger, setTriggerDuration } from "../../hooks/useBackend";

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

  // Prepare scatter data from monitoring frames
  const scatterData = monitoringValid.map((f) => ({
    area: f.validation.area,
    deformability: f.validation.deformability,
    isTargetGroup: f.validation.isTargetGroup,
  }));

  // Prepare histogram data from monitoring frames
  const histogramData = monitoringValid.map((f) => f.validation.ringRatio);

  return (
    <div
      className="grid h-full"
      style={{
        gridTemplateColumns: "1fr 1fr auto",
        gridTemplateRows: "auto 1fr 1fr",
        gap: "var(--spacing-sm)",
        padding: "var(--spacing-sm)",
      }}
    >
      {/* Row 0: Top controls (span 2 columns) */}
      <div className="flex items-center gap-2 col-span-2">
        <button
          onClick={handleClearBuffer}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          Clear Buffer
        </button>
        <button
          onClick={handleSortTrigger}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
          title="Manually fire a sort trigger pulse for testing"
        >
          Sort Trigger
        </button>
        <div className="flex items-center gap-1">
          <input
            type="number"
            min={1}
            max={1000000}
            value={triggerDurationUs}
            onChange={(e) => handleTriggerDurationChange(Number(e.target.value))}
            className="w-24 px-1 py-0.5 text-xs border border-neutral-400 rounded"
            title="Trigger pulse duration in microseconds"
          />
          <span className="text-xs text-neutral-600">us</span>
        </div>
        <div className="flex-1" />
      </div>

      {/* Row 1, Col 0: Scatter plot */}
      <div className="min-h-0">
        <ScatterPlot data={scatterData} />
      </div>

      {/* Row 1, Col 1: Histogram */}
      <div className="min-h-0">
        <Histogram data={histogramData} />
      </div>

      {/* Row 2, Col 0: Valid frames */}
      <div className="flex flex-col min-h-0">
        <div className="flex items-center gap-2 mb-1">
          <label className="flex items-center gap-1 text-xs">
            <input
              type="checkbox"
              checked={validOverlay}
              onChange={(e) => setValidOverlay(e.target.checked)}
            />
            Show Overlay
          </label>
          <div className="flex-1" />
        </div>
        <div className="flex-1 min-h-[200px] overflow-y-auto border border-neutral-300 bg-white">
          <FrameGrid frames={monitoringValid} showOverlay={validOverlay} />
        </div>
      </div>

      {/* Row 2, Col 1: Invalid frames */}
      <div className="flex flex-col min-h-0">
        <div className="flex items-center gap-2 mb-1">
          <label className="flex items-center gap-1 text-xs">
            <input
              type="checkbox"
              checked={invalidOverlay}
              onChange={(e) => setInvalidOverlay(e.target.checked)}
            />
            Show Overlay
          </label>
          <div className="flex-1" />
        </div>
        <div className="flex-1 min-h-[200px] overflow-y-auto border border-neutral-300 bg-white">
          <FrameGrid frames={monitoringInvalid} showOverlay={invalidOverlay} />
        </div>
      </div>

      {/* Rows 0-2, Col 2: Tune params panel */}
      <div className="row-span-3 min-h-0" style={{ minWidth: 0, maxWidth: 280 }}>
        <TuneParamsPanel />
      </div>
    </div>
  );
}
