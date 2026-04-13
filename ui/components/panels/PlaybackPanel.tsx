import { useState, useCallback } from "react";
import { ImageCanvas } from "../canvas/ImageCanvas";
import { usePlaybackStore } from "../../stores/playbackStore";
import { useProcessingStore } from "../../stores/processingStore";
import { useExperimentStore } from "../../stores/experimentStore";
import { useAppStore } from "../../stores/appStore";
import { usePlayback } from "../../hooks/usePlayback";
import {
  setRealtimeBackground,
  setRealtimeRoi,
  startFrameRecording,
  stopFrameRecording,
} from "../../hooks/useBackend";
import type { OverlayMode } from "../../types/backend";
import { Button } from "../ui/button";
import { Checkbox } from "../ui/checkbox";
import { Label } from "../ui/label";
import { Slider } from "../ui/slider";

const OVERLAY_CYCLE: OverlayMode[] = ["off", "mask", "contours", "both"];
const OVERLAY_LABELS: Record<OverlayMode, string> = {
  off: "Overlay: Off",
  mask: "Overlay: Mask",
  contours: "Overlay: Contours",
  both: "Overlay: Both",
};

export function PlaybackPanel() {
  const zoomMode = usePlaybackStore((s) => s.zoomMode);
  const toggleZoom = usePlaybackStore((s) => s.toggleZoomMode);
  const earliest = usePlaybackStore((s) => s.earliest);
  const latest = usePlaybackStore((s) => s.latest);
  const currentIndex = usePlaybackStore((s) => s.currentFrameIndex);
  const overlayMode = useProcessingStore((s) => s.overlayMode);
  const setOverlayMode = useProcessingStore((s) => s.setOverlayMode);
  const roi = useProcessingStore((s) => s.roi);
  const setRoi = useProcessingStore((s) => s.setRoi);
  const isRecording = useExperimentStore((s) => s.isRecording);
  const setRecording = useExperimentStore((s) => s.setRecording);
  const openDialog = useAppStore((s) => s.openDialog);
  const { seekToIndex } = usePlayback();

  const [autoBg, setAutoBg] = useState(false);

  const handleOverlayToggle = useCallback(() => {
    const idx = OVERLAY_CYCLE.indexOf(overlayMode);
    const next = OVERLAY_CYCLE[(idx + 1) % OVERLAY_CYCLE.length];
    setOverlayMode(next);
  }, [overlayMode, setOverlayMode]);

  const handleSetBackground = async () => {
    try {
      await setRealtimeBackground();
    } catch (e) {
      console.error("Failed to set background:", e);
    }
  };

  const handleClearRoi = async () => {
    setRoi(null);
    try {
      await setRealtimeRoi(null);
    } catch {
      // Ignore
    }
  };

  const handleToggleRecording = async () => {
    if (isRecording) {
      try {
        await stopFrameRecording();
        setRecording(false);
      } catch (e) {
        console.error("Failed to stop recording:", e);
      }
    } else {
      try {
        await startFrameRecording("recording.h5");
        setRecording(true);
      } catch (e) {
        console.error("Failed to start recording:", e);
      }
    }
  };

  return (
    <div className="flex flex-col h-full">
      {/* Canvas area */}
      <div className="flex-1 min-h-0">
        <ImageCanvas />
      </div>

      {/* Controls bar */}
      <div className="flex items-center px-2 py-1 gap-2 flex-shrink-0">
        <Button variant="ghost" size="sm" onClick={handleOverlayToggle}>
          {OVERLAY_LABELS[overlayMode]}
        </Button>

        {overlayMode !== "off" && (
          <span className="overlay-legend flex items-center gap-1">
            <span className="swatch" style={{ backgroundColor: "var(--color-target)" }} /> Target
            <span className="swatch" style={{ backgroundColor: "var(--color-valid)" }} /> Valid
            <span className="swatch" style={{ backgroundColor: "var(--color-invalid)" }} /> Invalid
          </span>
        )}

        <Button variant="ghost" size="sm" onClick={handleSetBackground}>Set BG</Button>

        <div className="flex items-center gap-1.5">
          <Checkbox
            id="auto-bg"
            checked={autoBg}
            onCheckedChange={(v) => setAutoBg(v === true)}
          />
          <Label htmlFor="auto-bg" className="text-xs">Auto BG</Label>
        </div>

        <Button variant="ghost" size="sm" onClick={handleClearRoi} disabled={!roi}>
          Clear ROI
        </Button>

        <Button variant="ghost" size="sm" onClick={() => openDialog("bufferSave")}>
          Save Buffer
        </Button>

        <Button
          variant="ghost"
          size="sm"
          onClick={handleToggleRecording}
          className={isRecording ? "text-destructive" : ""}
        >
          {isRecording ? "Stop Rec" : "Record"}
        </Button>

        {isRecording && (
          <span className="text-xs text-muted-foreground">Recording...</span>
        )}

        <Button variant="ghost" size="sm" onClick={toggleZoom}>
          Fit: {zoomMode === "fit" ? "Window" : "100%"}
        </Button>

        <div className="flex-1" />
      </div>

      {/* Playback slider */}
      <div className="px-2 pb-1 flex-shrink-0">
        <Slider
          min={earliest}
          max={latest || 1}
          step={1}
          value={[currentIndex]}
          onValueChange={([v]) => seekToIndex(v)}
        />
      </div>
    </div>
  );
}
