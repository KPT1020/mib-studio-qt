import { useState } from "react";
import { PreviewPage } from "./PreviewPage";
import { MonitoringTab } from "./MonitoringTab";
import { useExperimentStore } from "../../stores/experimentStore";
import { useProcessingStore } from "../../stores/processingStore";
import { startExperiment, stopExperiment } from "../../hooks/useBackend";

export function ExperimentTab() {
  const [activeTab, setActiveTab] = useState<"preview" | "monitoring">("preview");
  const isActive = useExperimentStore((s) => s.isActive);
  const roi = useProcessingStore((s) => s.roi);

  const handleStartExperiment = async () => {
    try {
      await startExperiment("experiment.h5");
      useExperimentStore.getState().setActive(true);
    } catch (e) {
      console.error("Failed to start experiment:", e);
    }
  };

  const handleStopExperiment = async () => {
    try {
      await stopExperiment();
      useExperimentStore.getState().setActive(false);
    } catch (e) {
      console.error("Failed to stop experiment:", e);
    }
  };

  return (
    /* QTabWidget with 2 tabs: "Preview" and "Monitoring" */
    <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
      {/* QTabWidget header row with corner widget (top-right of tab bar) */}
      <div style={{ display: "flex", alignItems: "center", flexShrink: 0 }}>
        {/* Tab bar */}
        <div className="qt-tab-bar" style={{ flex: "none" }}>
          <button
            className="qt-tab"
            data-active={activeTab === "preview"}
            onClick={() => setActiveTab("preview")}
          >
            Preview
          </button>
          <button
            className="qt-tab"
            data-active={activeTab === "monitoring"}
            onClick={() => setActiveTab("monitoring")}
          >
            Monitoring
          </button>
        </div>

        {/* Spacer between tabs and corner widget */}
        <div style={{ flex: 1, borderBottom: "1px solid #c0c0c0", background: "#ececec" }} />

        {/* Corner widget (top-right of tab bar): QHBoxLayout */}
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 6,
            padding: "0 6px",
            borderBottom: "1px solid #c0c0c0",
            background: "#ececec",
          }}
        >
          {/* Experiment indicator: 20x20 div, gray bg + 1px black border (green when active) */}
          <div
            style={{
              width: 20,
              height: 20,
              border: "1px solid black",
              backgroundColor: isActive ? "#00cc00" : "#c0c0c0",
              flexShrink: 0,
            }}
          />

          {/* ROI label: "ROI: W x H @ (X, Y)" with font-weight bold, padding 0 8px */}
          {roi && (
            <span style={{ fontSize: 12, fontWeight: "bold", padding: "0 8px" }}>
              ROI: {roi.w} x {roi.h} @ ({roi.x}, {roi.y})
            </span>
          )}

          {/* Start Experiment button (QPushButton) */}
          <button
            className="qt-btn"
            onClick={handleStartExperiment}
            disabled={isActive}
          >
            Start Experiment
          </button>

          {/* Stop Experiment button (QPushButton) */}
          <button
            className="qt-btn"
            onClick={handleStopExperiment}
            disabled={!isActive}
          >
            Stop Experiment
          </button>
        </div>
      </div>

      {/* Tab content */}
      <div style={{ flex: 1, minHeight: 0, overflow: "hidden" }}>
        {activeTab === "preview" && <PreviewPage />}
        {activeTab === "monitoring" && <MonitoringTab />}
      </div>
    </div>
  );
}
