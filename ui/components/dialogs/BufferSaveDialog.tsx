import { useState } from "react";
import { usePlaybackStore } from "../../stores/playbackStore";
import { saveBufferToDisk } from "../../hooks/useBackend";
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

export function BufferSaveDialog({ onClose }: Props) {
  const earliest = usePlaybackStore((s) => s.earliest);
  const latest = usePlaybackStore((s) => s.latest);
  const [outputDir, setOutputDir] = useState("");
  const [startIndex, setStartIndex] = useState(earliest);
  const [endIndex, setEndIndex] = useState(latest);
  const [saving, setSaving] = useState(false);

  const handleBrowse = async () => {
    // TODO: Use Tauri dialog.open to select output directory
  };

  const handleSave = async () => {
    setSaving(true);
    try {
      await saveBufferToDisk(outputDir, startIndex, endIndex);
    } catch (e) {
      console.error("Failed to save buffer:", e);
    }
    setSaving(false);
    onClose();
  };

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-w-md">
        <DialogHeader>
          <DialogTitle>Save Buffer to Disk</DialogTitle>
          <DialogDescription>
            Export frame buffer to disk. Range: {earliest} - {latest} ({latest - earliest + 1} frames available)
          </DialogDescription>
        </DialogHeader>

        <div className="form-grid">
          <Label>Output Directory:</Label>
          <div className="flex gap-1">
            <Input
              type="text"
              value={outputDir}
              onChange={(e) => setOutputDir(e.target.value)}
              className="flex-1"
            />
            <Button size="sm" variant="outline" onClick={handleBrowse}>Browse...</Button>
          </div>
          <Label>Start Index:</Label>
          <Input
            type="number"
            min={earliest}
            max={latest}
            value={startIndex}
            onChange={(e) => setStartIndex(Number(e.target.value))}
          />
          <Label>End Index:</Label>
          <Input
            type="number"
            min={earliest}
            max={latest}
            value={endIndex}
            onChange={(e) => setEndIndex(Number(e.target.value))}
          />
        </div>

        <DialogFooter>
          <Button variant="outline" onClick={onClose}>Cancel</Button>
          <Button onClick={handleSave} disabled={saving || !outputDir}>
            {saving ? "Saving..." : "Save"}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
