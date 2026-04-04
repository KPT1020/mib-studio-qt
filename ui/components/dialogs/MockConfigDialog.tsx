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

  const handleBrowse = async () => {
    // TODO: Use Tauri dialog.open to select directory
  };

  const handleOk = async () => {
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

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-[var(--bg-window)] shadow-xl flex flex-col" style={{ width: 500, minHeight: 150 }}>
        <div className="px-3 py-2 border-b border-[var(--border-widget)] font-semibold text-sm">
          Mock Camera Settings
        </div>
        <div className="flex-1 px-3 py-3">
          <div className="qt-form">
            <label>Frame folder</label>
            <div className="flex gap-1">
              <input
                type="text"
                value={directory}
                onChange={(e) => setDirectory(e.target.value)}
                placeholder="Select a folder containing image frames"
                className="qt-input flex-1"
              />
              <button onClick={handleBrowse} className="qt-btn">Browse...</button>
            </div>
            <label>Frame rate</label>
            <div className="flex items-center gap-1">
              <input
                type="number"
                min={1}
                max={10000}
                value={Math.round(1000 / intervalMs)}
                onChange={(e) => setIntervalMs(Math.round(1000 / Number(e.target.value)))}
                className="qt-input flex-1"
              />
              <span className="text-xs">fps</span>
            </div>
          </div>
        </div>
        <div className="flex justify-end gap-2 px-3 py-2 border-t border-[var(--border-widget)]">
          <button onClick={onClose} className="qt-btn">Cancel</button>
          <button onClick={handleOk} className="qt-btn">OK</button>
        </div>
      </div>
    </div>
  );
}
