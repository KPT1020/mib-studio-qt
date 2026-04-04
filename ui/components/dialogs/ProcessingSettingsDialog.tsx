import { useState } from "react";
import { useProcessingStore } from "../../stores/processingStore";
import { setProcessingConfig } from "../../hooks/useBackend";
import type { ProcessingConfig } from "../../types/backend";

interface Props {
  onClose: () => void;
}

export function ProcessingSettingsDialog({ onClose }: Props) {
  const config = useProcessingStore((s) => s.config);
  const setConfig = useProcessingStore((s) => s.setConfig);
  const [local, setLocal] = useState<ProcessingConfig>({ ...config });

  const update = (patch: Partial<ProcessingConfig>) =>
    setLocal((prev) => ({ ...prev, ...patch }));

  const handleApply = async () => {
    setConfig(local);
    try {
      await setProcessingConfig(local);
    } catch (e) {
      console.error("Failed to apply processing config:", e);
    }
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-white rounded-lg shadow-xl w-[500px] max-h-[80vh] flex flex-col">
        <div className="px-4 py-3 border-b border-neutral-300 font-semibold text-sm">
          Processing Settings
        </div>
        <div className="flex-1 overflow-y-auto px-4 py-3">
          <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-2 text-xs">
            <label className="text-right">Gaussian Blur Size:</label>
            <input
              type="number" min={1} step={2} value={local.gaussian_blur_size}
              onChange={(e) => update({ gaussian_blur_size: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">BG Subtract Threshold:</label>
            <input
              type="number" min={0} value={local.bg_subtract_threshold}
              onChange={(e) => update({ bg_subtract_threshold: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">Morph Kernel Size:</label>
            <input
              type="number" min={1} step={2} value={local.morph_kernel_size}
              onChange={(e) => update({ morph_kernel_size: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">Morph Iterations:</label>
            <input
              type="number" min={0} value={local.morph_iterations}
              onChange={(e) => update({ morph_iterations: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">Empty Frame Threshold:</label>
            <input
              type="number" min={0} value={local.empty_frame_pixel_threshold}
              onChange={(e) => update({ empty_frame_pixel_threshold: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">Auto BG Empty Frames:</label>
            <input
              type="number" min={0} value={local.auto_background_empty_frames}
              onChange={(e) => update({ auto_background_empty_frames: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
            <label className="text-right">Auto BG Cooldown:</label>
            <input
              type="number" min={0} value={local.auto_background_cooldown_frames}
              onChange={(e) => update({ auto_background_cooldown_frames: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5"
            />
          </div>
        </div>
        <div className="flex justify-end gap-2 px-4 py-3 border-t border-neutral-300">
          <button
            onClick={onClose}
            className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
          >
            Cancel
          </button>
          <button
            onClick={handleApply}
            className="px-3 py-1 text-xs bg-blue-600 text-white rounded hover:bg-blue-700"
          >
            Apply
          </button>
        </div>
      </div>
    </div>
  );
}
