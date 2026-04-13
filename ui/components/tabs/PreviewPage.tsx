import { ResizableSplitter } from "../layout/ResizableSplitter";
import { PlaybackPanel } from "../panels/PlaybackPanel";
import { ConfigTabs } from "../panels/ConfigTabs";
import { useCaptureStore } from "../../stores/captureStore";
import { startCapture, stopCapture } from "../../hooks/useBackend";
import { Button } from "../ui/button";
import { Play, Square } from "lucide-react";

export function PreviewPage() {
  const isRunning = useCaptureStore((s) => s.isRunning);

  const handlePlay = async () => {
    try {
      await startCapture();
      useCaptureStore.getState().setRunning(true);
    } catch (e) {
      console.error("Failed to start:", e);
    }
  };

  const handleStop = async () => {
    try {
      await stopCapture();
      useCaptureStore.getState().setRunning(false);
    } catch (e) {
      console.error("Failed to stop:", e);
    }
  };

  return (
    <div className="h-full">
      <ResizableSplitter direction="vertical" defaultSizes={[50, 50]} minSizes={[100, 0]} handleSize={10}>
        {/* Playback with overlay buttons */}
        <div className="relative h-full">
          <PlaybackPanel />

          {/* Play/Stop overlay when not running */}
          {!isRunning && (
            <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
              <div className="flex items-center gap-3 pointer-events-auto">
                <Button size="lg" onClick={handlePlay} className="min-w-[120px] min-h-[48px] text-base gap-2">
                  <Play className="h-5 w-5" />
                  Play
                </Button>
                <Button size="lg" variant="outline" onClick={handleStop} className="min-w-[120px] min-h-[48px] text-base gap-2">
                  <Square className="h-5 w-5" />
                  Stop
                </Button>
              </div>
            </div>
          )}
        </div>

        {/* Config tabs */}
        <div className="h-full">
          <ConfigTabs />
        </div>
      </ResizableSplitter>
    </div>
  );
}
