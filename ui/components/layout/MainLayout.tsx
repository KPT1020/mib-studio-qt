import { useAppStore } from "../../stores/appStore";
import { Sidebar } from "./Sidebar";
import { TabBar } from "./TabBar";
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

  const disabledTabs = new Set<string>();
  if (isExperimentActive) {
    disabledTabs.add("overview");
    disabledTabs.add("review");
  }

  const cornerWidget = (
    <div className="flex items-center gap-1">
      <button
        className="qt-btn"
        onClick={handleStartCamera}
        disabled={isRunning}
      >
        Start Camera
      </button>
      <button
        className="qt-btn"
        onClick={handleStopCamera}
        disabled={!isRunning}
      >
        Stop Camera
      </button>
    </div>
  );

  return (
    <div className="flex h-full">
      {/* LEFT: SidebarWidget (collapsible) */}
      <Sidebar />

      {/* RIGHT: QTabWidget with 4 tabs */}
      <div className="flex-1 flex flex-col min-w-0">
        <TabBar
          tabs={TABS}
          activeTab={activeTab}
          onTabChange={(id) => setActiveTab(id as ActiveTab)}
          disabledTabs={disabledTabs}
          cornerWidget={cornerWidget}
        >
          {activeTab === "connect" && <ConnectTab />}
          {activeTab === "overview" && <OverviewTab />}
          {activeTab === "experiment" && <ExperimentTab />}
          {activeTab === "review" && <ReviewTab />}
        </TabBar>
      </div>
    </div>
  );
}
