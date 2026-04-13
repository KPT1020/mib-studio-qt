import { PreviewPage } from "./PreviewPage";
import { MonitoringTab } from "./MonitoringTab";
import { useExperimentStore } from "../../stores/experimentStore";
import { useProcessingStore } from "../../stores/processingStore";
import { startExperiment, stopExperiment } from "../../hooks/useBackend";
import { Tabs, TabsList, TabsTrigger, TabsContent } from "../ui/tabs";
import { Button } from "../ui/button";
import { Badge } from "../ui/badge";

export function ExperimentTab() {
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
    <Tabs defaultValue="preview" className="flex flex-col h-full">
      <div className="flex items-end border-b border-border bg-muted/30">
        <TabsList className="border-b-0">
          <TabsTrigger value="preview">Preview</TabsTrigger>
          <TabsTrigger value="monitoring">Monitoring</TabsTrigger>
        </TabsList>

        {/* Spacer + corner widget */}
        <div className="ml-auto flex items-center gap-2 px-2 pb-1">
          {/* Experiment indicator */}
          <div
            className="w-4 h-4 rounded-full border-2 flex-shrink-0"
            style={{
              backgroundColor: isActive ? "var(--color-indicator-active)" : "var(--color-indicator-idle)",
              borderColor: isActive ? "var(--color-indicator-active)" : "var(--color-indicator-idle)",
            }}
          />

          {roi && (
            <Badge variant="secondary" className="font-mono text-xs">
              ROI: {roi.w} x {roi.h} @ ({roi.x}, {roi.y})
            </Badge>
          )}

          <Button size="sm" onClick={handleStartExperiment} disabled={isActive}>
            Start Experiment
          </Button>
          <Button size="sm" variant="outline" onClick={handleStopExperiment} disabled={!isActive}>
            Stop Experiment
          </Button>
        </div>
      </div>

      <TabsContent value="preview" className="overflow-hidden">
        <PreviewPage />
      </TabsContent>
      <TabsContent value="monitoring" className="overflow-hidden">
        <MonitoringTab />
      </TabsContent>
    </Tabs>
  );
}
