import { useState } from "react";
import { setPixelToMicronFactor } from "../../hooks/useBackend";
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
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-w-sm">
        <DialogHeader>
          <DialogTitle>Pixel to Micron Conversion</DialogTitle>
          <DialogDescription>Set the conversion factor for pixel measurements.</DialogDescription>
        </DialogHeader>

        <div className="form-grid">
          <Label>Factor</Label>
          <div className="flex items-center gap-2">
            <Input
              type="number"
              min={0.0001}
              max={1000}
              step={0.0001}
              value={factor}
              onChange={(e) => setFactor(Number(e.target.value))}
              title="Conversion factor: 1 pixel = X micron"
              className="flex-1"
            />
            <span className="text-xs text-muted-foreground">um/pixel</span>
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
