import { ResizableSplitter } from "../layout/ResizableSplitter";
import { PlaybackPanel } from "../panels/PlaybackPanel";
import { ConfigTabs } from "../panels/ConfigTabs";
import { useCaptureStore } from "../../stores/captureStore";
import { startCapture, stopCapture } from "../../hooks/useBackend";

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
    <ResizableSplitter direction="vertical" defaultSizes={[50, 50]} minSizes={[100, 0]}>
      {/* Top: PlaybackPanel with play/stop overlay */}
      <div className="relative h-full">
        <PlaybackPanel />

        {/* Play/Stop overlay - shown when capture not running */}
        {!isRunning && (
          <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
            <div className="flex items-center gap-3 pointer-events-auto">
              <button
                onClick={handlePlay}
                className="px-6 py-3 text-sm font-medium bg-green-600 text-white rounded-lg hover:bg-green-700 shadow-lg"
                style={{ minWidth: 120, minHeight: 48 }}
              >
                &#9654; Play
              </button>
              <button
                onClick={handleStop}
                className="px-6 py-3 text-sm font-medium bg-red-600 text-white rounded-lg hover:bg-red-700 shadow-lg"
                style={{ minWidth: 120, minHeight: 48 }}
              >
                &#9632; Stop
              </button>
            </div>
          </div>
        )}
      </div>

      {/* Bottom: ConfigTabs */}
      <ConfigTabs />
    </ResizableSplitter>
  );
}
