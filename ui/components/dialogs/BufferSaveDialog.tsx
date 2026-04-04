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

  const handleBrowse = async () => {
    // TODO: Use Tauri dialog.open to select output directory
  };

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
      <div className="bg-[var(--bg-window)] shadow-xl flex flex-col" style={{ width: 450 }}>
        <div className="px-3 py-2 border-b border-[var(--border-widget)] font-semibold text-sm">
          Save Buffer to Disk
        </div>
        <div className="px-3 py-3">
          <div className="qt-form">
            <label>Output Directory:</label>
            <div className="flex gap-1">
              <input
                type="text"
                value={outputDir}
                onChange={(e) => setOutputDir(e.target.value)}
                className="qt-input flex-1"
              />
              <button onClick={handleBrowse} className="qt-btn">Browse...</button>
            </div>
            <label>Start Index:</label>
            <input
              type="number"
              min={earliest}
              max={latest}
              value={startIndex}
              onChange={(e) => setStartIndex(Number(e.target.value))}
              className="qt-input"
            />
            <label>End Index:</label>
            <input
              type="number"
              min={earliest}
              max={latest}
              value={endIndex}
              onChange={(e) => setEndIndex(Number(e.target.value))}
              className="qt-input"
            />
          </div>
          <p className="text-xs mt-2" style={{ color: "var(--text-secondary)" }}>
            Range: {earliest} - {latest} ({latest - earliest + 1} frames available)
          </p>
        </div>
        <div className="flex justify-end gap-2 px-3 py-2 border-t border-[var(--border-widget)]">
          <button onClick={onClose} className="qt-btn">Cancel</button>
          <button onClick={handleSave} disabled={saving || !outputDir} className="qt-btn">
            {saving ? "Saving..." : "Save"}
          </button>
        </div>
      </div>
    </div>
  );
}
