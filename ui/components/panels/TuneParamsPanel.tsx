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
    <div
      style={{
        minWidth: 0,
        maxWidth: 280,
        height: "100%",
        overflowY: "auto",
      }}
    >
      <div
        style={{
          display: "flex",
          flexDirection: "column",
          padding: 8,
          gap: 8,
        }}
      >
        {/* 1. Title: "Tune Params" (bold) */}
        <span style={{ fontWeight: "bold", fontSize: 12 }}>Tune Params</span>

        {/* 2. Filter Thresholds QGroupBox */}
        <fieldset className="qt-groupbox">
          <legend>Filter Thresholds</legend>
          <div className="qt-form">
            <label>Area Min:</label>
            <input
              className="qt-input"
              type="number"
              value={local.area_threshold_min}
              onChange={(e) => update({ area_threshold_min: Number(e.target.value) })}
            />
            <label>Area Max:</label>
            <input
              className="qt-input"
              type="number"
              value={local.area_threshold_max}
              onChange={(e) => update({ area_threshold_max: Number(e.target.value) })}
            />
            <label>Deform Min:</label>
            <input
              className="qt-input"
              type="number"
              step={0.01}
              value={local.deformability_threshold_min}
              onChange={(e) =>
                update({ deformability_threshold_min: Number(e.target.value) })
              }
            />
            <label>Deform Max:</label>
            <input
              className="qt-input"
              type="number"
              step={0.01}
              value={local.deformability_threshold_max}
              onChange={(e) =>
                update({ deformability_threshold_max: Number(e.target.value) })
              }
            />
            <label>Area Ratio Max:</label>
            <input
              className="qt-input"
              type="number"
              step={0.01}
              value={local.area_ratio_threshold_max}
              onChange={(e) =>
                update({ area_ratio_threshold_max: Number(e.target.value) })
              }
            />
          </div>
        </fieldset>

        {/* 3. Filter Enables QGroupBox */}
        <fieldset className="qt-groupbox">
          <legend>Filter Enables</legend>
          <div style={{ display: "flex", flexDirection: "column", gap: 4 }}>
            <label className="qt-checkbox">
              <input
                type="checkbox"
                checked={local.enable_border_check}
                onChange={(e) => update({ enable_border_check: e.target.checked })}
              />
              Border Check
            </label>
            <label className="qt-checkbox">
              <input
                type="checkbox"
                checked={local.enable_area_range_check}
                onChange={(e) => update({ enable_area_range_check: e.target.checked })}
              />
              Area Range
            </label>
            <label className="qt-checkbox">
              <input
                type="checkbox"
                checked={local.enable_deformability_range_check}
                onChange={(e) =>
                  update({ enable_deformability_range_check: e.target.checked })
                }
              />
              Deformability Range
            </label>
            <label className="qt-checkbox">
              <input
                type="checkbox"
                checked={local.enable_area_ratio_check}
                onChange={(e) => update({ enable_area_ratio_check: e.target.checked })}
              />
              Area Ratio
            </label>
            <label className="qt-checkbox">
              <input
                type="checkbox"
                checked={local.require_single_inner_contour}
                onChange={(e) =>
                  update({ require_single_inner_contour: e.target.checked })
                }
              />
              Single Inner Contour
            </label>
          </div>
        </fieldset>

        {/* 4. Target Group QGroupBox */}
        <fieldset className="qt-groupbox">
          <legend>Target Group</legend>
          <div style={{ display: "flex", flexDirection: "column", gap: 4 }}>
            <label className="qt-checkbox">
              <input
                type="checkbox"
                checked={local.enable_target_group}
                onChange={(e) => update({ enable_target_group: e.target.checked })}
              />
              Enable
            </label>
            <div className="qt-form">
              <label>Area Min:</label>
              <input
                className="qt-input"
                type="number"
                value={local.target_group_area_min}
                onChange={(e) =>
                  update({ target_group_area_min: Number(e.target.value) })
                }
              />
              <label>Area Max:</label>
              <input
                className="qt-input"
                type="number"
                value={local.target_group_area_max}
                onChange={(e) =>
                  update({ target_group_area_max: Number(e.target.value) })
                }
              />
              <label>Deform Min:</label>
              <input
                className="qt-input"
                type="number"
                step={0.01}
                value={local.target_group_deformability_min}
                onChange={(e) =>
                  update({ target_group_deformability_min: Number(e.target.value) })
                }
              />
              <label>Deform Max:</label>
              <input
                className="qt-input"
                type="number"
                step={0.01}
                value={local.target_group_deformability_max}
                onChange={(e) =>
                  update({ target_group_deformability_max: Number(e.target.value) })
                }
              />
            </div>
          </div>
        </fieldset>

        {/* 5. Apply QPushButton */}
        <button className="qt-btn" onClick={handleApply} style={{ width: "100%" }}>
          Apply
        </button>

        {/* 6. Stretch */}
        <div style={{ flex: 1 }} />
      </div>
    </div>
  );
}
