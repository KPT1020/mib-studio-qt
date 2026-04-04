import { useState } from "react";
import { useProcessingStore } from "../../stores/processingStore";
import { setProcessingConfig } from "../../hooks/useBackend";
import type { ProcessingConfig } from "../../types/backend";

export function TuneParamsPanel() {
  const config = useProcessingStore((s) => s.config);
  const setConfig = useProcessingStore((s) => s.setConfig);

  const [local, setLocal] = useState<ProcessingConfig>(config);

  const update = (patch: Partial<ProcessingConfig>) => {
    setLocal((prev) => ({ ...prev, ...patch }));
  };

  const handleApply = async () => {
    setConfig(local);
    try {
      await setProcessingConfig(local);
    } catch (e) {
      console.error("Failed to apply config:", e);
    }
  };

  return (
    <div className="h-full overflow-y-auto p-1.5" style={{ maxWidth: 280 }}>
      <h3 className="text-sm font-bold mb-2">Tune Params</h3>

      {/* Filter Thresholds */}
      <fieldset className="border border-neutral-300 rounded p-2 mb-2">
        <legend className="text-xs font-semibold px-1">Filter Thresholds</legend>
        <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-1 text-xs">
          <label className="text-right">Area Min:</label>
          <input
            type="number"
            value={local.area_threshold_min}
            onChange={(e) => update({ area_threshold_min: Number(e.target.value) })}
            className="border border-neutral-400 rounded px-1 py-0.5 w-full"
          />
          <label className="text-right">Area Max:</label>
          <input
            type="number"
            value={local.area_threshold_max}
            onChange={(e) => update({ area_threshold_max: Number(e.target.value) })}
            className="border border-neutral-400 rounded px-1 py-0.5 w-full"
          />
          <label className="text-right">Deform Min:</label>
          <input
            type="number"
            step={0.01}
            value={local.deformability_threshold_min}
            onChange={(e) => update({ deformability_threshold_min: Number(e.target.value) })}
            className="border border-neutral-400 rounded px-1 py-0.5 w-full"
          />
          <label className="text-right">Deform Max:</label>
          <input
            type="number"
            step={0.01}
            value={local.deformability_threshold_max}
            onChange={(e) => update({ deformability_threshold_max: Number(e.target.value) })}
            className="border border-neutral-400 rounded px-1 py-0.5 w-full"
          />
          <label className="text-right">Area Ratio Max:</label>
          <input
            type="number"
            step={0.01}
            value={local.area_ratio_threshold_max}
            onChange={(e) => update({ area_ratio_threshold_max: Number(e.target.value) })}
            className="border border-neutral-400 rounded px-1 py-0.5 w-full"
          />
        </div>
      </fieldset>

      {/* Filter Enables */}
      <fieldset className="border border-neutral-300 rounded p-2 mb-2">
        <legend className="text-xs font-semibold px-1">Filter Enables</legend>
        <div className="flex flex-col gap-1 text-xs">
          <label className="flex items-center gap-1">
            <input
              type="checkbox"
              checked={local.enable_border_check}
              onChange={(e) => update({ enable_border_check: e.target.checked })}
            />
            Border Check
          </label>
          <label className="flex items-center gap-1">
            <input
              type="checkbox"
              checked={local.enable_area_range_check}
              onChange={(e) => update({ enable_area_range_check: e.target.checked })}
            />
            Area Range
          </label>
          <label className="flex items-center gap-1">
            <input
              type="checkbox"
              checked={local.enable_deformability_range_check}
              onChange={(e) => update({ enable_deformability_range_check: e.target.checked })}
            />
            Deformability Range
          </label>
          <label className="flex items-center gap-1">
            <input
              type="checkbox"
              checked={local.enable_area_ratio_check}
              onChange={(e) => update({ enable_area_ratio_check: e.target.checked })}
            />
            Area Ratio
          </label>
          <label className="flex items-center gap-1">
            <input
              type="checkbox"
              checked={local.require_single_inner_contour}
              onChange={(e) => update({ require_single_inner_contour: e.target.checked })}
            />
            Single Inner Contour
          </label>
        </div>
      </fieldset>

      {/* Target Group */}
      <fieldset className="border border-neutral-300 rounded p-2 mb-2">
        <legend className="text-xs font-semibold px-1">Target Group</legend>
        <div className="flex flex-col gap-1 text-xs">
          <label className="flex items-center gap-1">
            <input
              type="checkbox"
              checked={local.enable_target_group}
              onChange={(e) => update({ enable_target_group: e.target.checked })}
            />
            Enable
          </label>
          <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-1">
            <label className="text-right">Area Min:</label>
            <input
              type="number"
              value={local.target_group_area_min}
              onChange={(e) => update({ target_group_area_min: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5 w-full"
            />
            <label className="text-right">Area Max:</label>
            <input
              type="number"
              value={local.target_group_area_max}
              onChange={(e) => update({ target_group_area_max: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5 w-full"
            />
            <label className="text-right">Deform Min:</label>
            <input
              type="number"
              step={0.01}
              value={local.target_group_deformability_min}
              onChange={(e) => update({ target_group_deformability_min: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5 w-full"
            />
            <label className="text-right">Deform Max:</label>
            <input
              type="number"
              step={0.01}
              value={local.target_group_deformability_max}
              onChange={(e) => update({ target_group_deformability_max: Number(e.target.value) })}
              className="border border-neutral-400 rounded px-1 py-0.5 w-full"
            />
          </div>
        </div>
      </fieldset>

      {/* Apply button */}
      <button
        onClick={handleApply}
        className="w-full px-3 py-1.5 text-xs bg-blue-600 text-white rounded hover:bg-blue-700"
      >
        Apply
      </button>
    </div>
  );
}
