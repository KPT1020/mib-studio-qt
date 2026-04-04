import { useState } from "react";
import { TabBar } from "../layout/TabBar";
import { PreviewPage } from "./PreviewPage";
import { MonitoringTab } from "./MonitoringTab";
import { useExperimentStore } from "../../stores/experimentStore";
import { useProcessingStore } from "../../stores/processingStore";
import { startExperiment, stopExperiment } from "../../hooks/useBackend";

const EXPERIMENT_TABS = [
  { id: "preview", label: "Preview" },
  { id: "monitoring", label: "Monitoring" },
];

export function ExperimentTab() {
  const [activeTab, setActiveTab] = useState("preview");
  const isActive = useExperimentStore((s) => s.isActive);
  const roi = useProcessingStore((s) => s.roi);

  const handleStartExperiment = async () => {
    // In production, this would open a file dialog first
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

  const cornerWidget = (
    <div className="flex items-center gap-2">
      {/* Experiment indicator */}
      <div
        className="border border-black"
        style={{
          width: 20,
          height: 20,
          backgroundColor: isActive
            ? "var(--color-indicator-active)"
            : "var(--color-indicator-idle)",
        }}
      />

      {/* ROI display */}
      {roi && (
        <span className="text-xs font-bold px-2">
          ROI: {roi.w} x {roi.h} @ ({roi.x}, {roi.y})
        </span>
      )}

      {/* Start/Stop buttons */}
      <button
        onClick={handleStartExperiment}
        disabled={isActive}
        className="px-3 py-1 text-xs bg-green-600 text-white rounded hover:bg-green-700 disabled:opacity-50 disabled:cursor-not-allowed"
      >
        Start Experiment
      </button>
      <button
        onClick={handleStopExperiment}
        disabled={!isActive}
        className="px-3 py-1 text-xs bg-red-600 text-white rounded hover:bg-red-700 disabled:opacity-50 disabled:cursor-not-allowed"
      >
        Stop Experiment
      </button>
    </div>
  );

  return (
    <TabBar
      tabs={EXPERIMENT_TABS}
      activeTab={activeTab}
      onTabChange={setActiveTab}
      cornerWidget={cornerWidget}
    >
      {activeTab === "preview" && <PreviewPage />}
      {activeTab === "monitoring" && <MonitoringTab />}
    </TabBar>
  );
}
