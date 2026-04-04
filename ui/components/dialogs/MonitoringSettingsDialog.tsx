import { useState } from "react";

interface Props {
  onClose: () => void;
}

export function MonitoringSettingsDialog({ onClose }: Props) {
  const [updateInterval, setUpdateInterval] = useState(500);
  const [maxThumbnails, setMaxThumbnails] = useState(50);

  const handleApply = () => {
    // TODO: Apply settings via Tauri command
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-white rounded-lg shadow-xl w-[350px] flex flex-col">
        <div className="px-4 py-3 border-b border-neutral-300 font-semibold text-sm">
          Monitoring Settings
        </div>
        <div className="px-4 py-3">
          <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-2 text-xs">
            <label className="text-right">Update Interval (ms):</label>
            <input
              type="number" min={100} max={10000} value={updateInterval}
              onChange={(e) => setUpdateInterval(Number(e.target.value))}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">Max Thumbnails:</label>
            <input
              type="number" min={1} max={500} value={maxThumbnails}
              onChange={(e) => setMaxThumbnails(Number(e.target.value))}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
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
