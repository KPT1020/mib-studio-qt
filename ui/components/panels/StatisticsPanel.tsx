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
    <div className="p-3">
      <span className="font-semibold text-sm block mb-2">Statistics</span>
      <div className="form-grid">
        <label>Capture FPS:</label>
        <span className="font-mono text-xs">{frameRate.toFixed(1)}</span>

        <label>Data Rate (MB/s):</label>
        <span className="font-mono text-xs">{dataRate.toFixed(1)}</span>

        <label>Frames:</label>
        <span className="font-mono text-xs">{framesProcessed}</span>

        <label>Algo FPS:</label>
        <span className="font-mono text-xs">{algoFps.toFixed(1)}</span>

        <label>Valid FPS:</label>
        <span className="font-mono text-xs">{validFps.toFixed(1)}</span>

        <label>Invalid FPS:</label>
        <span className="font-mono text-xs">{invalidFps.toFixed(1)}</span>

        <label>Algo Avg (us):</label>
        <span className="font-mono text-xs">{algoAvgUs.toFixed(0)}</span>

        <label>Total Valid:</label>
        <span className="font-mono text-xs">{totalValidFlushed}</span>
      </div>
    </div>
  );
}
