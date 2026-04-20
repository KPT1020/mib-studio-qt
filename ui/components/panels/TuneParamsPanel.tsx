import { useState } from "react";
import { useProcessingStore } from "../../stores/processingStore";
import { setProcessingConfig } from "../../hooks/useBackend";
import type { ProcessingConfig } from "../../types/backend";
import { Button } from "../ui/button";
import { Input } from "../ui/input";
import { Checkbox } from "../ui/checkbox";
import { Label } from "../ui/label";
import { Card, CardContent, CardHeader, CardTitle } from "../ui/card";

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
    <div className="min-w-0 max-w-[280px] h-full overflow-y-auto">
      <div className="flex flex-col p-2 gap-2">
        <span className="font-semibold text-sm">Tune Params</span>

        {/* Filter Thresholds */}
        <Card>
          <CardHeader className="p-3 pb-0">
            <CardTitle>Filter Thresholds</CardTitle>
          </CardHeader>
          <CardContent className="p-3">
            <div className="form-grid">
              <Label>Area Min:</Label>
              <Input
                type="number"
                value={local.area_threshold_min}
                onChange={(e) => update({ area_threshold_min: Number(e.target.value) })}
              />
              <Label>Area Max:</Label>
              <Input
                type="number"
                value={local.area_threshold_max}
                onChange={(e) => update({ area_threshold_max: Number(e.target.value) })}
              />
              <Label>Deform Min:</Label>
              <Input
                type="number"
                step={0.01}
                value={local.deformability_threshold_min}
                onChange={(e) => update({ deformability_threshold_min: Number(e.target.value) })}
              />
              <Label>Deform Max:</Label>
              <Input
                type="number"
                step={0.01}
                value={local.deformability_threshold_max}
                onChange={(e) => update({ deformability_threshold_max: Number(e.target.value) })}
              />
              <Label>Area Ratio Max:</Label>
              <Input
                type="number"
                step={0.01}
                value={local.area_ratio_threshold_max}
                onChange={(e) => update({ area_ratio_threshold_max: Number(e.target.value) })}
              />
            </div>
          </CardContent>
        </Card>

        {/* Filter Enables */}
        <Card>
          <CardHeader className="p-3 pb-0">
            <CardTitle>Filter Enables</CardTitle>
          </CardHeader>
          <CardContent className="p-3">
            <div className="flex flex-col gap-2">
              <div className="flex items-center gap-2">
                <Checkbox
                  id="border-check"
                  checked={local.enable_border_check}
                  onCheckedChange={(v) => update({ enable_border_check: v === true })}
                />
                <Label htmlFor="border-check" className="text-xs">Border Check</Label>
              </div>
              <div className="flex items-center gap-2">
                <Checkbox
                  id="area-range"
                  checked={local.enable_area_range_check}
                  onCheckedChange={(v) => update({ enable_area_range_check: v === true })}
                />
                <Label htmlFor="area-range" className="text-xs">Area Range</Label>
              </div>
              <div className="flex items-center gap-2">
                <Checkbox
                  id="deform-range"
                  checked={local.enable_deformability_range_check}
                  onCheckedChange={(v) => update({ enable_deformability_range_check: v === true })}
                />
                <Label htmlFor="deform-range" className="text-xs">Deformability Range</Label>
              </div>
              <div className="flex items-center gap-2">
                <Checkbox
                  id="area-ratio"
                  checked={local.enable_area_ratio_check}
                  onCheckedChange={(v) => update({ enable_area_ratio_check: v === true })}
                />
                <Label htmlFor="area-ratio" className="text-xs">Area Ratio</Label>
              </div>
              <div className="flex items-center gap-2">
                <Checkbox
                  id="single-contour"
                  checked={local.require_single_inner_contour}
                  onCheckedChange={(v) => update({ require_single_inner_contour: v === true })}
                />
                <Label htmlFor="single-contour" className="text-xs">Single Inner Contour</Label>
              </div>
            </div>
          </CardContent>
        </Card>

        {/* Target Group */}
        <Card>
          <CardHeader className="p-3 pb-0">
            <CardTitle>Target Group</CardTitle>
          </CardHeader>
          <CardContent className="p-3">
            <div className="flex flex-col gap-2">
              <div className="flex items-center gap-2">
                <Checkbox
                  id="target-enable"
                  checked={local.enable_target_group}
                  onCheckedChange={(v) => update({ enable_target_group: v === true })}
                />
                <Label htmlFor="target-enable" className="text-xs">Enable</Label>
              </div>
              <div className="form-grid">
                <Label>Area Min:</Label>
                <Input
                  type="number"
                  value={local.target_group_area_min}
                  onChange={(e) => update({ target_group_area_min: Number(e.target.value) })}
                />
                <Label>Area Max:</Label>
                <Input
                  type="number"
                  value={local.target_group_area_max}
                  onChange={(e) => update({ target_group_area_max: Number(e.target.value) })}
                />
                <Label>Deform Min:</Label>
                <Input
                  type="number"
                  step={0.01}
                  value={local.target_group_deformability_min}
                  onChange={(e) => update({ target_group_deformability_min: Number(e.target.value) })}
                />
                <Label>Deform Max:</Label>
                <Input
                  type="number"
                  step={0.01}
                  value={local.target_group_deformability_max}
                  onChange={(e) => update({ target_group_deformability_max: Number(e.target.value) })}
                />
              </div>
            </div>
          </CardContent>
        </Card>

        <Button onClick={handleApply} className="w-full">Apply</Button>
        <div className="flex-1" />
      </div>
    </div>
  );
}
