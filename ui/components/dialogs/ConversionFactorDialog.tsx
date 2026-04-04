import { useState } from "react";
import { setPixelToMicronFactor } from "../../hooks/useBackend";

interface Props {
  onClose: () => void;
}

export function ConversionFactorDialog({ onClose }: Props) {
  const [factor, setFactor] = useState(1.0);

  const handleApply = async () => {
    try {
      await setPixelToMicronFactor(factor);
    } catch (e) {
      console.error("Failed to set conversion factor:", e);
    }
  };

  const handleOk = async () => {
    await handleApply();
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-[var(--bg-window)] shadow-xl flex flex-col" style={{ width: 400, minHeight: 120 }}>
        <div className="px-3 py-2 border-b border-[var(--border-widget)] font-semibold text-sm">
          Pixel to Micron Conversion
        </div>
        <div className="flex-1 px-3 py-3">
          <div className="qt-form">
            <label>Pixel to Micron Factor</label>
            <div className="flex items-center gap-1">
              <input
                type="number"
                min={0.0001}
                max={1000}
                step={0.0001}
                value={factor}
                onChange={(e) => setFactor(Number(e.target.value))}
                className="qt-input flex-1"
                title="Conversion factor: 1 pixel = X micron"
              />
              <span className="text-xs">um/pixel</span>
            </div>
          </div>
        </div>
        <div className="flex justify-end gap-2 px-3 py-2 border-t border-[var(--border-widget)]">
          <button onClick={onClose} className="qt-btn">Cancel</button>
          <button onClick={handleApply} className="qt-btn">Apply</button>
          <button onClick={handleOk} className="qt-btn">OK</button>
        </div>
      </div>
    </div>
  );
}
