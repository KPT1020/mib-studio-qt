import { useState } from "react";
import { configureMock } from "../../hooks/useBackend";
import { useCaptureStore } from "../../stores/captureStore";
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

export function MockConfigDialog({ onClose }: Props) {
  const [directory, setDirectory] = useState("");
  const [intervalMs, setIntervalMs] = useState(33);

  const handleBrowse = async () => {
    // TODO: Use Tauri dialog.open to select directory
  };

  const handleOk = async () => {
    const options = { directory, intervalMs, loop: true };
    try {
      await configureMock(options);
      useCaptureStore.getState().setMockOptions(options);
      useCaptureStore.getState().setCameraConfigured(true);
    } catch (e) {
      console.error("Failed to configure mock camera:", e);
    }
    onClose();
  };

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-w-lg">
        <DialogHeader>
          <DialogTitle>Mock Camera Settings</DialogTitle>
          <DialogDescription>Configure the mock camera image source.</DialogDescription>
        </DialogHeader>

        <div className="form-grid">
          <Label>Frame folder</Label>
          <div className="flex gap-1">
            <Input
              type="text"
              value={directory}
              onChange={(e) => setDirectory(e.target.value)}
              placeholder="Select a folder containing image frames"
              className="flex-1"
            />
            <Button size="sm" variant="outline" onClick={handleBrowse}>Browse...</Button>
          </div>
          <Label>Frame rate</Label>
          <div className="flex items-center gap-2">
            <Input
              type="number"
              min={1}
              max={10000}
              value={Math.round(1000 / intervalMs)}
              onChange={(e) => setIntervalMs(Math.round(1000 / Number(e.target.value)))}
              className="flex-1"
            />
            <span className="text-xs text-muted-foreground">fps</span>
          </div>
        </div>

        <DialogFooter>
          <Button variant="outline" onClick={onClose}>Cancel</Button>
          <Button onClick={handleOk}>OK</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
