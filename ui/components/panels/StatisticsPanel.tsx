import { useCaptureStore } from "../../stores/captureStore";
import { useProcessingStore } from "../../stores/processingStore";

export function StatisticsPanel() {
  const frameRate = useCaptureStore((s) => s.frameRate);
  const dataRate = useCaptureStore((s) => s.dataRateMBps);
  const framesProcessed = useCaptureStore((s) => s.framesProcessed);
  const algoFps = useProcessingStore((s) => s.algoFps);
  const validFps = useProcessingStore((s) => s.validFps);
  const invalidFps = useProcessingStore((s) => s.invalidFps);
  const algoAvgUs = useProcessingStore((s) => s.algoAvgUs);
  const totalValidFlushed = useProcessingStore((s) => s.totalValidFlushed);

  return (
    <div style={{ padding: "var(--spacing-sm)" }}>
      <span
        style={{
          fontWeight: "bold",
          fontSize: 12,
          display: "block",
          marginBottom: 4,
        }}
      >
        Statistics
      </span>
      <div className="qt-form">
        <label>Capture FPS:</label>
        <span style={{ fontFamily: "monospace" }}>{frameRate.toFixed(1)}</span>

        <label>Data Rate (MB/s):</label>
        <span style={{ fontFamily: "monospace" }}>{dataRate.toFixed(1)}</span>

        <label>Frames:</label>
        <span style={{ fontFamily: "monospace" }}>{framesProcessed}</span>

        <label>Algo FPS:</label>
        <span style={{ fontFamily: "monospace" }}>{algoFps.toFixed(1)}</span>

        <label>Valid FPS:</label>
        <span style={{ fontFamily: "monospace" }}>{validFps.toFixed(1)}</span>

        <label>Invalid FPS:</label>
        <span style={{ fontFamily: "monospace" }}>{invalidFps.toFixed(1)}</span>

        <label>Algo Avg (us):</label>
        <span style={{ fontFamily: "monospace" }}>{algoAvgUs.toFixed(0)}</span>

        <label>Total Valid:</label>
        <span style={{ fontFamily: "monospace" }}>{totalValidFlushed}</span>
      </div>
    </div>
  );
}
