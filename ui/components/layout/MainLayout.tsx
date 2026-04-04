import { useAppStore } from "../../stores/appStore";
import { Sidebar } from "./Sidebar";
import { ConnectTab } from "../tabs/ConnectTab";
import { OverviewTab } from "../tabs/OverviewTab";
import { ExperimentTab } from "../tabs/ExperimentTab";
import { ReviewTab } from "../tabs/ReviewTab";
import { useCaptureStore } from "../../stores/captureStore";
import { useExperimentStore } from "../../stores/experimentStore";
import { startCapture, stopCapture } from "../../hooks/useBackend";
import type { ActiveTab } from "../../stores/appStore";

const TABS: { id: ActiveTab; label: string }[] = [
  { id: "connect", label: "Connect" },
  { id: "overview", label: "Overview" },
  { id: "experiment", label: "Experiment" },
  { id: "review", label: "Review" },
];

export function MainLayout() {
  const activeTab = useAppStore((s) => s.activeTab);
  const setActiveTab = useAppStore((s) => s.setActiveTab);
  const sidebarCollapsed = useAppStore((s) => s.sidebarCollapsed);
  const isRunning = useCaptureStore((s) => s.isRunning);
  const isExperimentActive = useExperimentStore((s) => s.isActive);

  const handleStartCamera = async () => {
    try {
      await startCapture();
      useCaptureStore.getState().setRunning(true);
      useAppStore.getState().setStatusText("Camera running");
    } catch (e) {
      console.error("Failed to start capture:", e);
    }
  };

  const handleStopCamera = async () => {
    try {
      await stopCapture();
      useCaptureStore.getState().setRunning(false);
      useAppStore.getState().setStatusText("Camera stopped");
    } catch (e) {
      console.error("Failed to stop capture:", e);
    }
  };

  return (
    <div className="flex h-full">
      {/* Sidebar */}
      <Sidebar />

      {/* Main tab area */}
      <div className="flex-1 flex flex-col min-w-0">
        {/* Tab bar with corner controls */}
        <div className="flex items-center border-b border-neutral-300 bg-neutral-100">
          {/* Tab buttons */}
          <div className="flex">
            {TABS.map((tab) => {
              const disabled =
                (tab.id === "overview" && isExperimentActive) ||
                (tab.id === "review" && isExperimentActive);

              return (
                <button
                  key={tab.id}
                  onClick={() => !disabled && setActiveTab(tab.id)}
                  disabled={disabled}
                  className={`px-4 py-2 text-sm border-r border-neutral-300 transition-colors ${
                    activeTab === tab.id
                      ? "bg-white font-semibold border-b-2 border-b-blue-500"
                      : "hover:bg-neutral-200"
                  } ${disabled ? "opacity-50 cursor-not-allowed" : "cursor-pointer"}`}
                >
                  {tab.label}
                </button>
              );
            })}
          </div>

          {/* Spacer */}
          <div className="flex-1" />

          {/* Camera controls (corner widget) */}
          <div className="flex items-center gap-1 px-2">
            <button
              onClick={handleStartCamera}
              disabled={isRunning}
              className="px-3 py-1 text-xs bg-green-600 text-white rounded hover:bg-green-700 disabled:opacity-50 disabled:cursor-not-allowed"
            >
              Start Camera
            </button>
            <button
              onClick={handleStopCamera}
              disabled={!isRunning}
              className="px-3 py-1 text-xs bg-red-600 text-white rounded hover:bg-red-700 disabled:opacity-50 disabled:cursor-not-allowed"
            >
              Stop Camera
            </button>
          </div>
        </div>

        {/* Tab content */}
        <div className="flex-1 min-h-0 overflow-hidden">
          {activeTab === "connect" && <ConnectTab />}
          {activeTab === "overview" && <OverviewTab />}
          {activeTab === "experiment" && <ExperimentTab />}
          {activeTab === "review" && <ReviewTab />}
        </div>
      </div>
    </div>
  );
}
