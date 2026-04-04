import { useState } from "react";
import { useProcessingStore } from "../../stores/processingStore";
import { setProcessingConfig, setRealtimeRoi } from "../../hooks/useBackend";
import type { ProcessingConfig, Roi } from "../../types/backend";

interface Props {
  onClose: () => void;
}

export function ProcessingSettingsDialog({ onClose }: Props) {
  const config = useProcessingStore((s) => s.config);
  const roi = useProcessingStore((s) => s.roi);
  const setConfig = useProcessingStore((s) => s.setConfig);
  const setRoi = useProcessingStore((s) => s.setRoi);

  const [local, setLocal] = useState<ProcessingConfig>({ ...config });
  const [localRoi, setLocalRoi] = useState<Roi>(
    roi ?? { x: 0, y: 0, w: 1, h: 1 }
  );
  const [invalidSampling, setInvalidSampling] = useState(1);
  const [flushInterval, setFlushInterval] = useState(100);
  const [dropFrames, setDropFrames] = useState(false);

  const update = (patch: Partial<ProcessingConfig>) =>
    setLocal((prev) => ({ ...prev, ...patch }));

  const handleApply = async () => {
    setConfig(local);
    setRoi(localRoi);
    try {
      await setProcessingConfig(local);
      await setRealtimeRoi(localRoi);
    } catch (e) {
      console.error("Failed to apply processing config:", e);
    }
  };

  const handleOk = async () => {
    await handleApply();
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-[var(--bg-window)] shadow-xl flex flex-col" style={{ width: 400, minHeight: 300 }}>
        {/* Title bar */}
        <div className="px-3 py-2 border-b border-[var(--border-widget)] font-semibold text-sm">
          Processing Settings
        </div>

        {/* Form content */}
        <div className="flex-1 overflow-y-auto px-3 py-3">
          <div className="qt-form">
            <label>Invalid frame sampling</label>
            <input
              type="number"
              min={1}
              max={10000}
              value={invalidSampling}
              onChange={(e) => setInvalidSampling(Number(e.target.value))}
              className="qt-input"
              title="Save every Nth invalid frame (1 = save all, higher = fewer frames)"
            />

            <label>Flush interval</label>
            <input
              type="number"
              min={1}
              max={10000}
              value={flushInterval}
              onChange={(e) => setFlushInterval(Number(e.target.value))}
              className="qt-input"
              title="Flush buffered frames to HDF5 every N frames"
            />

            {/* ROI section header */}
            <label
              className="col-span-2 text-left font-bold mt-2"
              style={{ gridColumn: "1 / -1" }}
            >
              Region of Interest (ROI)
            </label>

            <label>ROI X</label>
            <input
              type="number"
              min={0}
              value={localRoi.x}
              onChange={(e) => setLocalRoi((r) => ({ ...r, x: Number(e.target.value) }))}
              className="qt-input"
              title="ROI X position (left edge)"
            />

            <label>ROI Y</label>
            <input
              type="number"
              min={0}
              value={localRoi.y}
              onChange={(e) => setLocalRoi((r) => ({ ...r, y: Number(e.target.value) }))}
              className="qt-input"
              title="ROI Y position (top edge)"
            />

            <label>ROI Width</label>
            <input
              type="number"
              min={1}
              value={localRoi.w}
              onChange={(e) => setLocalRoi((r) => ({ ...r, w: Number(e.target.value) }))}
              className="qt-input"
              title="ROI width"
            />

            <label>ROI Height</label>
            <input
              type="number"
              min={1}
              value={localRoi.h}
              onChange={(e) => setLocalRoi((r) => ({ ...r, h: Number(e.target.value) }))}
              className="qt-input"
              title="ROI height"
            />

            <label>Realtime</label>
            <label className="qt-checkbox">
              <input
                type="checkbox"
                checked={dropFrames}
                onChange={(e) => setDropFrames(e.target.checked)}
              />
              Process latest only (drop intermediate frames)
            </label>
          </div>
        </div>

        {/* Button box (matching QDialogButtonBox: Cancel | OK | Apply) */}
        <div className="flex justify-end gap-2 px-3 py-2 border-t border-[var(--border-widget)]">
          <button onClick={onClose} className="qt-btn">
            Cancel
          </button>
          <button onClick={handleApply} className="qt-btn">
            Apply
          </button>
          <button onClick={handleOk} className="qt-btn">
            OK
          </button>
        </div>
      </div>
    </div>
  );
}
