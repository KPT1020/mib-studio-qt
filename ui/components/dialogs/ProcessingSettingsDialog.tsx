import { useState } from "react";
import { useProcessingStore } from "../../stores/processingStore";
import { setProcessingConfig, setRealtimeRoi } from "../../hooks/useBackend";
import type { ProcessingConfig, Roi } from "../../types/backend";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
  DialogDescription,
} from "../ui/dialog";
import { Button } from "../ui/button";
import { Input } from "../ui/input";
import { Label } from "../ui/label";
import { Checkbox } from "../ui/checkbox";

interface Props {
  onClose: () => void;
}

export function ProcessingSettingsDialog({ onClose }: Props) {
  const config = useProcessingStore((s) => s.config);
  const roi = useProcessingStore((s) => s.roi);
  const setConfig = useProcessingStore((s) => s.setConfig);
  const setRoi = useProcessingStore((s) => s.setRoi);

  const [local] = useState<ProcessingConfig>({ ...config });
  const [localRoi, setLocalRoi] = useState<Roi>(
    roi ?? { x: 0, y: 0, w: 1, h: 1 }
  );
  const [invalidSampling, setInvalidSampling] = useState(1);
  const [flushInterval, setFlushInterval] = useState(100);
  const [dropFrames, setDropFrames] = useState(false);

  const handleApply = async () => {
    setConfig(local);
    setRoi(localRoi);
    try {
      await setProcessingConfig(local);
      await setRealtimeRoi(localRoi);
    } catch (e) {
      console.error("Failed to apply processing config:", e);
    }
  };

  const handleOk = async () => {
    await handleApply();
    onClose();
  };

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-w-md">
        <DialogHeader>
          <DialogTitle>Processing Settings</DialogTitle>
          <DialogDescription>Configure processing parameters and ROI.</DialogDescription>
        </DialogHeader>

        <div className="space-y-3 max-h-[60vh] overflow-y-auto pr-1">
          <div className="form-grid">
            <Label>Invalid frame sampling</Label>
            <Input
              type="number"
              min={1}
              max={10000}
              value={invalidSampling}
              onChange={(e) => setInvalidSampling(Number(e.target.value))}
              title="Save every Nth invalid frame (1 = save all, higher = fewer frames)"
            />

            <Label>Flush interval</Label>
            <Input
              type="number"
              min={1}
              max={10000}
              value={flushInterval}
              onChange={(e) => setFlushInterval(Number(e.target.value))}
              title="Flush buffered frames to HDF5 every N frames"
            />
          </div>

          <div className="pt-2">
            <span className="font-semibold text-sm">Region of Interest (ROI)</span>
          </div>

          <div className="form-grid">
            <Label>ROI X</Label>
            <Input
              type="number"
              min={0}
              value={localRoi.x}
              onChange={(e) => setLocalRoi((r) => ({ ...r, x: Number(e.target.value) }))}
            />
            <Label>ROI Y</Label>
            <Input
              type="number"
              min={0}
              value={localRoi.y}
              onChange={(e) => setLocalRoi((r) => ({ ...r, y: Number(e.target.value) }))}
            />
            <Label>ROI Width</Label>
            <Input
              type="number"
              min={1}
              value={localRoi.w}
              onChange={(e) => setLocalRoi((r) => ({ ...r, w: Number(e.target.value) }))}
            />
            <Label>ROI Height</Label>
            <Input
              type="number"
              min={1}
              value={localRoi.h}
              onChange={(e) => setLocalRoi((r) => ({ ...r, h: Number(e.target.value) }))}
            />
          </div>

          <div className="flex items-center gap-2 pt-1">
            <Checkbox
              id="drop-frames"
              checked={dropFrames}
              onCheckedChange={(v) => setDropFrames(v === true)}
            />
            <Label htmlFor="drop-frames" className="text-xs">
              Process latest only (drop intermediate frames)
            </Label>
          </div>
        </div>

        <DialogFooter>
          <Button variant="outline" onClick={onClose}>Cancel</Button>
          <Button variant="secondary" onClick={handleApply}>Apply</Button>
          <Button onClick={handleOk}>OK</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
