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
    <div className="p-1.5" style={{ padding: "var(--spacing-sm)" }}>
      <h4 className="text-xs font-bold mb-1">Statistics</h4>
      <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-0.5 text-xs">
        <span className="text-right text-neutral-600">Capture FPS:</span>
        <span className="font-mono">{frameRate.toFixed(1)}</span>

        <span className="text-right text-neutral-600">Data Rate:</span>
        <span className="font-mono">{dataRate.toFixed(1)} MB/s</span>

        <span className="text-right text-neutral-600">Frames:</span>
        <span className="font-mono">{framesProcessed}</span>

        <span className="text-right text-neutral-600">Algo FPS:</span>
        <span className="font-mono">{algoFps.toFixed(1)}</span>

        <span className="text-right text-neutral-600">Valid FPS:</span>
        <span className="font-mono">{validFps.toFixed(1)}</span>

        <span className="text-right text-neutral-600">Invalid FPS:</span>
        <span className="font-mono">{invalidFps.toFixed(1)}</span>

        <span className="text-right text-neutral-600">Algo Avg:</span>
        <span className="font-mono">{algoAvgUs.toFixed(0)} us</span>

        <span className="text-right text-neutral-600">Total Valid:</span>
        <span className="font-mono">{totalValidFlushed}</span>
      </div>
    </div>
  );
}
