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
    /* QVBoxLayout margins 0, spacing 0 */
    <div style={{ height: "100%", margin: 0, padding: 0 }}>
      {/* QSplitter vertical, childrenCollapsible=false, handleWidth=10, opaqueResize=true */}
      <ResizableSplitter direction="vertical" defaultSizes={[50, 50]} minSizes={[100, 0]} handleSize={10}>
        {/* Child 1 (overlayContainer): Expanding/Expanding, minHeight=100 */}
        {/* QStackedLayout(StackAll): PlaybackPanel at bottom, play/stop overlay on top */}
        <div style={{ position: "relative", height: "100%" }}>
          {/* PlaybackPanel at bottom of stack */}
          <PlaybackPanel />

          {/* Play/Stop overlay: centered horizontally, visible when capture NOT running */}
          {!isRunning && (
            <div
              style={{
                position: "absolute",
                inset: 0,
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                pointerEvents: "none",
              }}
            >
              <div style={{ display: "flex", alignItems: "center", pointerEvents: "auto" }}>
                {/* Play button: "Play" (min 120x48) */}
                <button
                  className="qt-btn"
                  onClick={handlePlay}
                  style={{ minWidth: 120, minHeight: 48, fontSize: 14 }}
                >
                  &#9654; Play
                </button>
                {/* 12px spacer */}
                <div style={{ width: 12 }} />
                {/* Stop button: "Stop" (min 120x48) */}
                <button
                  className="qt-btn"
                  onClick={handleStop}
                  style={{ minWidth: 120, minHeight: 48, fontSize: 14 }}
                >
                  &#9632; Stop
                </button>
              </div>
            </div>
          )}
        </div>

        {/* Child 2 (configTabsPlaceholder): Expanding/Ignored, minHeight=0 */}
        <div style={{ height: "100%" }}>
          <ConfigTabs />
        </div>
      </ResizableSplitter>
    </div>
  );
}
