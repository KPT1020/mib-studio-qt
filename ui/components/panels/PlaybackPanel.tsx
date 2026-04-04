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
      <div className="flex items-center gap-1.5 px-1.5 py-1 flex-shrink-0 border-t border-neutral-200">
        <button
          onClick={handleOverlayToggle}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          {OVERLAY_LABELS[overlayMode]}
        </button>

        {overlayMode !== "off" && (
          <span className="text-xs">
            <span style={{ color: "var(--color-target)" }}>&#9632;</span> Target{" "}
            <span style={{ color: "var(--color-valid)" }}>&#9632;</span> Valid{" "}
            <span style={{ color: "var(--color-invalid)" }}>&#9632;</span> Invalid
          </span>
        )}

        <button
          onClick={handleSetBackground}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          Set BG
        </button>

        <label className="flex items-center gap-1 text-xs">
          <input
            type="checkbox"
            checked={autoBg}
            onChange={(e) => setAutoBg(e.target.checked)}
          />
          Auto BG
        </label>

        <button
          onClick={handleClearRoi}
          disabled={!roi}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
        >
          Clear ROI
        </button>

        <button
          onClick={() => openDialog("bufferSave")}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          Save Buffer
        </button>

        <button
          onClick={handleToggleRecording}
          className={`px-2 py-0.5 text-xs border rounded ${
            isRecording
              ? "bg-red-100 border-red-400 text-red-700"
              : "bg-neutral-200 hover:bg-neutral-300 border-neutral-400"
          }`}
        >
          {isRecording ? "Stop Rec" : "Record"}
        </button>

        {isRecording && (
          <span className="text-xs" style={{ color: "gray", padding: "0 4px" }}>
            Recording...
          </span>
        )}

        <button
          onClick={toggleZoom}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          Fit: {zoomMode === "fit" ? "Window" : "100%"}
        </button>

        <div className="flex-1" />
      </div>

      {/* Scrubbing slider */}
      <input
        type="range"
        min={earliest}
        max={latest}
        value={currentIndex}
        onChange={(e) => seekToIndex(Number(e.target.value))}
        className="w-full flex-shrink-0"
        step={1}
      />
    </div>
  );
}
