import { useState } from "react";
import { configureMock } from "../../hooks/useBackend";
import { useCaptureStore } from "../../stores/captureStore";

interface Props {
  onClose: () => void;
}

export function MockConfigDialog({ onClose }: Props) {
  const [directory, setDirectory] = useState("");
  const [intervalMs, setIntervalMs] = useState(33);
  const [loop, setLoop] = useState(true);

  const handleApply = async () => {
    const options = { directory, intervalMs, loop };
    try {
      await configureMock(options);
      useCaptureStore.getState().setMockOptions(options);
      useCaptureStore.getState().setCameraConfigured(true);
    } catch (e) {
      console.error("Failed to configure mock camera:", e);
    }
    onClose();
  };

  const handleBrowse = async () => {
    // TODO: Use Tauri dialog to select directory
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-white rounded-lg shadow-xl w-[450px] flex flex-col">
        <div className="px-4 py-3 border-b border-neutral-300 font-semibold text-sm">
          Configure Mock Camera
        </div>
        <div className="px-4 py-3">
          <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-2 text-xs">
            <label className="text-right">Image Directory:</label>
            <div className="flex gap-1">
              <input
                type="text" value={directory}
                onChange={(e) => setDirectory(e.target.value)}
                className="flex-1 border border-neutral-400 rounded px-1 py-0.5"
                placeholder="Path to TIFF/PNG/JPEG images"
              />
              <button
                onClick={handleBrowse}
                className="px-2 py-0.5 bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
              >
                Browse...
              </button>
            </div>
            <label className="text-right">Frame Interval (ms):</label>
            <input
              type="number" min={1} max={10000} value={intervalMs}
              onChange={(e) => setIntervalMs(Number(e.target.value))}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">Loop:</label>
            <label className="flex items-center gap-1">
              <input
                type="checkbox" checked={loop}
                onChange={(e) => setLoop(e.target.checked)}
              />
              Loop at end of sequence
            </label>
          </div>
        </div>
        <div className="flex justify-end gap-2 px-4 py-3 border-t border-neutral-300">
          <button onClick={onClose} className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">Cancel</button>
          <button onClick={handleApply} className="px-3 py-1 text-xs bg-blue-600 text-white rounded hover:bg-blue-700">Apply</button>
        </div>
      </div>
    </div>
  );
}
