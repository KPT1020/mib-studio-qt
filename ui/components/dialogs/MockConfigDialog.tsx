import { useState } from "react";
import { open } from "@tauri-apps/plugin-dialog";
import { configureMock } from "../../hooks/useBackend";
import { useCaptureStore } from "../../stores/captureStore";
import { useAppStore } from "../../stores/appStore";
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
  const [fps, setFps] = useState(30);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const setStatusText = useAppStore((s) => s.setStatusText);

  const handleBrowse = async () => {
    try {
      const selected = await open({
        directory: true,
        multiple: false,
        title: "Select Mock Frame Folder",
      });
      if (typeof selected === "string") {
        setDirectory(selected);
        setErrorMessage(null);
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      setErrorMessage(`Failed to open folder picker: ${message}`);
    }
  };

  const handleOk = async () => {
    const trimmedDirectory = directory.trim();
    if (!trimmedDirectory) {
      setErrorMessage("Please select a folder containing mock frame images.");
      return;
    }
    if (!Number.isFinite(fps) || fps < 1 || fps > 1000) {
      setErrorMessage("FPS must be between 1 and 1000.");
      return;
    }

    const intervalMs = Math.max(1, Math.round(1000 / fps));
    const options = { directory: trimmedDirectory, intervalMs, loop: true };
    try {
      await configureMock(options);
      useCaptureStore.getState().setMockOptions(options);
      useCaptureStore.getState().setCameraConfigured(true);
      setStatusText(`Mock camera configured (${fps} fps)`);
      setErrorMessage(null);
      onClose();
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      console.error("Failed to configure mock camera:", e);
      setErrorMessage(`Failed to configure mock camera: ${message}`);
    }
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
              onChange={(e) => {
                setDirectory(e.target.value);
                if (errorMessage) {
                  setErrorMessage(null);
                }
              }}
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
              max={1000}
              value={fps}
              onChange={(e) => {
                setFps(Number(e.target.value));
                if (errorMessage) {
                  setErrorMessage(null);
                }
              }}
              className="flex-1"
            />
            <span className="text-xs text-muted-foreground">fps</span>
          </div>
        </div>
        {errorMessage && (
          <p className="text-xs text-destructive">{errorMessage}</p>
        )}

        <DialogFooter>
          <Button variant="outline" onClick={onClose}>Cancel</Button>
          <Button onClick={handleOk} disabled={!directory.trim()}>OK</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
