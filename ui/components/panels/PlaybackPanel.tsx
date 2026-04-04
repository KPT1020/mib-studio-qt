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
    <div
      style={{
        display: "flex",
        flexDirection: "column",
        height: "100%",
        margin: 0,
        padding: 0,
        gap: 0,
      }}
    >
      {/* Canvas area: flex-1, contains ImageCanvas */}
      <div style={{ flex: 1, minHeight: 0 }}>
        <ImageCanvas />
      </div>

      {/* Controls bar: QHBoxLayout margins 6/4/6/4, spacing 6 */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          padding: "4px 6px 4px 6px",
          gap: 6,
          flexShrink: 0,
        }}
      >
        {/* QToolButton overlay toggle */}
        <button className="qt-tool-btn" onClick={handleOverlayToggle}>
          {OVERLAY_LABELS[overlayMode]}
        </button>

        {/* Overlay legend (shown when overlay != off) */}
        {overlayMode !== "off" && (
          <span
            className="overlay-legend"
            style={{ display: "flex", alignItems: "center", gap: 4 }}
          >
            <span
              className="swatch"
              style={{ backgroundColor: "#0078FF" }}
            />
            Target
            <span
              className="swatch"
              style={{ backgroundColor: "#00FF00" }}
            />
            Valid
            <span
              className="swatch"
              style={{ backgroundColor: "#FF0000" }}
            />
            Invalid
          </span>
        )}

        {/* QToolButton "Set BG" */}
        <button className="qt-tool-btn" onClick={handleSetBackground}>
          Set BG
        </button>

        {/* Auto Background checkbox */}
        <label className="qt-checkbox">
          <input
            type="checkbox"
            checked={autoBg}
            onChange={(e) => setAutoBg(e.target.checked)}
          />
          Auto Background
        </label>

        {/* QToolButton "Clear ROI" (disabled when no ROI) */}
        <button className="qt-tool-btn" onClick={handleClearRoi} disabled={!roi}>
          Clear ROI
        </button>

        {/* QToolButton "Save Buffer" */}
        <button className="qt-tool-btn" onClick={() => openDialog("bufferSave")}>
          Save Buffer
        </button>

        {/* QToolButton "Record" / "Stop Rec" */}
        <button
          className="qt-tool-btn"
          onClick={handleToggleRecording}
          style={isRecording ? { color: "#b91c1c" } : undefined}
        >
          {isRecording ? "Stop Rec" : "Record"}
        </button>

        {/* Record status label (color: gray, padding: 0 4px) */}
        {isRecording && (
          <span style={{ color: "gray", padding: "0 4px", fontSize: 12 }}>
            Recording...
          </span>
        )}

        {/* QToolButton "Fit: Window" / "Fit: 100%" */}
        <button className="qt-tool-btn" onClick={toggleZoom}>
          Fit: {zoomMode === "fit" ? "Window" : "100%"}
        </button>

        {/* Horizontal stretch */}
        <div style={{ flex: 1 }} />
      </div>

      {/* QSlider horizontal: range from earliest to latest, step 1, page step 8 */}
      <input
        type="range"
        min={earliest}
        max={latest}
        value={currentIndex}
        onChange={(e) => seekToIndex(Number(e.target.value))}
        step={1}
        style={{ width: "100%", flexShrink: 0, margin: 0, display: "block" }}
      />
    </div>
  );
}
