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
import { Tabs, TabsList, TabsTrigger, TabsContent } from "../ui/tabs";
import { Button } from "../ui/button";

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
  const cameraConfigured = useCaptureStore((s) => s.cameraConfigured);
  const setStatusText = useAppStore((s) => s.setStatusText);
  const isExperimentActive = useExperimentStore((s) => s.isActive);

  const handleStartCamera = async () => {
    if (!cameraConfigured) {
      setStatusText("Configure a camera before starting capture");
      return;
    }
    try {
      await startCapture();
      useCaptureStore.getState().setRunning(true);
      setStatusText("Camera running");
    } catch (e) {
      console.error("Failed to start capture:", e);
      const message = e instanceof Error ? e.message : String(e);
      useCaptureStore.getState().setRunning(false);
      setStatusText(`Camera start failed: ${message}`);
    }
  };

  const handleStopCamera = async () => {
    try {
      await stopCapture();
      useCaptureStore.getState().setRunning(false);
      setStatusText("Camera stopped");
    } catch (e) {
      console.error("Failed to stop capture:", e);
      const message = e instanceof Error ? e.message : String(e);
      setStatusText(`Camera stop failed: ${message}`);
    }
  };

  return (
    <div className="flex h-full">
      <Sidebar />

      <div className="flex-1 flex flex-col min-w-0">
        <Tabs value={activeTab} onValueChange={(v) => setActiveTab(v as ActiveTab)} className="flex flex-col h-full">
          <div className="flex items-end border-b border-border bg-muted/30">
            <TabsList className="border-b-0">
              {TABS.map((tab) => (
                <TabsTrigger
                  key={tab.id}
                  value={tab.id}
                  disabled={isExperimentActive && (tab.id === "overview" || tab.id === "review")}
                >
                  {tab.label}
                </TabsTrigger>
              ))}
            </TabsList>

            {/* Corner widget: camera controls */}
            <div className="ml-auto flex items-center gap-1 px-2 pb-1">
              <Button size="sm" onClick={handleStartCamera} disabled={isRunning || !cameraConfigured}>
                Start Camera
              </Button>
              <Button size="sm" variant="outline" onClick={handleStopCamera} disabled={!isRunning}>
                Stop Camera
              </Button>
            </div>
          </div>

          <TabsContent value="connect" className="overflow-hidden">
            <ConnectTab />
          </TabsContent>
          <TabsContent value="overview" className="overflow-hidden">
            <OverviewTab />
          </TabsContent>
          <TabsContent value="experiment" className="overflow-hidden">
            <ExperimentTab />
          </TabsContent>
          <TabsContent value="review" className="overflow-hidden">
            <ReviewTab />
          </TabsContent>
        </Tabs>
      </div>
    </div>
  );
}
