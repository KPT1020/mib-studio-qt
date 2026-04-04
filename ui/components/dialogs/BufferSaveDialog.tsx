import { useState } from "react";
import { usePlaybackStore } from "../../stores/playbackStore";
import { saveBufferToDisk } from "../../hooks/useBackend";

interface Props {
  onClose: () => void;
}

export function BufferSaveDialog({ onClose }: Props) {
  const earliest = usePlaybackStore((s) => s.earliest);
  const latest = usePlaybackStore((s) => s.latest);
  const [outputDir, setOutputDir] = useState("");
  const [startIndex, setStartIndex] = useState(earliest);
  const [endIndex, setEndIndex] = useState(latest);
  const [saving, setSaving] = useState(false);

  const handleSave = async () => {
    setSaving(true);
    try {
      await saveBufferToDisk(outputDir, startIndex, endIndex);
    } catch (e) {
      console.error("Failed to save buffer:", e);
    }
    setSaving(false);
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-white rounded-lg shadow-xl w-[450px] flex flex-col">
        <div className="px-4 py-3 border-b border-neutral-300 font-semibold text-sm">
          Save Buffer to Disk
        </div>
        <div className="px-4 py-3">
          <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-2 text-xs">
            <label className="text-right">Output Directory:</label>
            <div className="flex gap-1">
              <input
                type="text" value={outputDir}
                onChange={(e) => setOutputDir(e.target.value)}
                className="flex-1 border border-neutral-400 rounded px-1 py-0.5"
              />
              <button className="px-2 py-0.5 bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
                Browse...
              </button>
            </div>
            <label className="text-right">Start Index:</label>
            <input
              type="number" min={earliest} max={latest} value={startIndex}
              onChange={(e) => setStartIndex(Number(e.target.value))}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">End Index:</label>
            <input
              type="number" min={earliest} max={latest} value={endIndex}
              onChange={(e) => setEndIndex(Number(e.target.value))}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
          </div>
          <p className="text-xs text-neutral-500 mt-2">
            Range: {earliest} - {latest} ({latest - earliest + 1} frames available)
          </p>
        </div>
        <div className="flex justify-end gap-2 px-4 py-3 border-t border-neutral-300">
          <button onClick={onClose} className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">Cancel</button>
          <button
            onClick={handleSave}
            disabled={saving || !outputDir}
            className="px-3 py-1 text-xs bg-blue-600 text-white rounded hover:bg-blue-700 disabled:opacity-50"
          >
            {saving ? "Saving..." : "Save"}
          </button>
        </div>
      </div>
    </div>
  );
}
