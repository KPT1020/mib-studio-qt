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

  const scatterData = monitoringValid.map((f) => ({
    area: f.validation.area,
    deformability: f.validation.deformability,
    isTargetGroup: f.validation.isTargetGroup,
  }));

  const histogramData = monitoringValid.map((f) => f.validation.ringRatio);

  return (
    /* QGridLayout margins 6, spacing 6 */
    <div
      style={{
        display: "grid",
        gridTemplateColumns: "1fr 1fr auto",
        gridTemplateRows: "auto 1fr 1fr",
        gap: 6,
        padding: 6,
        height: "100%",
        minHeight: 0,
      }}
    >
      {/* Row 0, col 0-1: QHBoxLayout - Clear Buffer btn, Sort Trigger btn, QSpinBox (1-1000000 us, suffix " us"), spacer */}
      <div
        style={{
          gridColumn: "1 / 3",
          gridRow: 1,
          display: "flex",
          alignItems: "center",
          gap: 6,
        }}
      >
        <button className="qt-btn" onClick={handleClearBuffer}>
          Clear Buffer
        </button>
        <button
          className="qt-btn"
          onClick={handleSortTrigger}
          title="Manually fire a sort trigger pulse for testing"
        >
          Sort Trigger
        </button>
        <input
          type="number"
          className="qt-input"
          min={1}
          max={1000000}
          value={triggerDurationUs}
          onChange={(e) => handleTriggerDurationChange(Number(e.target.value))}
          style={{ width: 120 }}
          title="Trigger pulse duration in microseconds"
        />
        <span style={{ fontSize: 12 }}>us</span>
        <div style={{ flex: 1 }} />
      </div>

      {/* Row 1, col 0: QChartView (scatter plot) - Expanding/Expanding */}
      <div style={{ gridColumn: 1, gridRow: 2, minHeight: 0, minWidth: 0 }}>
        <ScatterPlot data={scatterData} />
      </div>

      {/* Row 1, col 1: QChartView (histogram) - Expanding/Expanding */}
      <div style={{ gridColumn: 2, gridRow: 2, minHeight: 0, minWidth: 0 }}>
        <Histogram data={histogramData} />
      </div>

      {/* Row 2, col 0: Valid frames container (QVBoxLayout margins 0, spacing 4) */}
      <div
        style={{
          gridColumn: 1,
          gridRow: 3,
          display: "flex",
          flexDirection: "column",
          gap: 4,
          minHeight: 0,
        }}
      >
        {/* Show Overlay checkbox + spacer */}
        <div style={{ display: "flex", alignItems: "center", flexShrink: 0 }}>
          <label className="qt-checkbox">
            <input
              type="checkbox"
              checked={validOverlay}
              onChange={(e) => setValidOverlay(e.target.checked)}
            />
            Show Overlay
          </label>
          <div style={{ flex: 1 }} />
        </div>
        {/* QScrollArea (min height 200, grid layout spacing 4 inside) */}
        <div
          style={{
            flex: 1,
            minHeight: 200,
            overflow: "auto",
            border: "1px solid #c0c0c0",
            background: "white",
          }}
        >
          <FrameGrid frames={monitoringValid} showOverlay={validOverlay} />
        </div>
      </div>

      {/* Row 2, col 1: Invalid frames container (same structure) */}
      <div
        style={{
          gridColumn: 2,
          gridRow: 3,
          display: "flex",
          flexDirection: "column",
          gap: 4,
          minHeight: 0,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", flexShrink: 0 }}>
          <label className="qt-checkbox">
            <input
              type="checkbox"
              checked={invalidOverlay}
              onChange={(e) => setInvalidOverlay(e.target.checked)}
            />
            Show Overlay
          </label>
          <div style={{ flex: 1 }} />
        </div>
        <div
          style={{
            flex: 1,
            minHeight: 200,
            overflow: "auto",
            border: "1px solid #c0c0c0",
            background: "white",
          }}
        >
          <FrameGrid frames={monitoringInvalid} showOverlay={invalidOverlay} />
        </div>
      </div>

      {/* Row 0-2, col 2: Tune params placeholder (minSize 0x0, spans all 3 rows) */}
      <div
        style={{
          gridColumn: 3,
          gridRow: "1 / 4",
          minWidth: 0,
          minHeight: 0,
        }}
      >
        <TuneParamsPanel />
      </div>
    </div>
  );
}
