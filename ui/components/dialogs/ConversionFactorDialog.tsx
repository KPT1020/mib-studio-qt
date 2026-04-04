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
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-white rounded-lg shadow-xl w-[350px] flex flex-col">
        <div className="px-4 py-3 border-b border-neutral-300 font-semibold text-sm">
          Pixel to Micron Conversion
        </div>
        <div className="px-4 py-3">
          <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-2 text-xs">
            <label className="text-right">Pixel to Micron Factor:</label>
            <input
              type="number" step={0.001} min={0.001} value={factor}
              onChange={(e) => setFactor(Number(e.target.value))}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
          </div>
          <p className="text-xs text-neutral-500 mt-2">
            1 pixel = {factor.toFixed(3)} um
          </p>
        </div>
        <div className="flex justify-end gap-2 px-4 py-3 border-t border-neutral-300">
          <button onClick={onClose} className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">Cancel</button>
          <button onClick={handleApply} className="px-3 py-1 text-xs bg-blue-600 text-white rounded hover:bg-blue-700">Apply</button>
        </div>
      </div>
    </div>
  );
}
