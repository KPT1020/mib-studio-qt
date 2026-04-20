import { create } from "zustand";
import type { ZoomMode } from "../types/backend";

interface PlaybackState {
  earliest: number;
  latest: number;
  count: number;
  pinnedIndex: number | null;
  isPlaying: boolean;
  zoomMode: ZoomMode;
  currentFrameBase64: string | null;
  currentFrameIndex: number;

  setRange: (earliest: number, latest: number, count: number) => void;
  setPinnedIndex: (index: number | null) => void;
  setPlaying: (playing: boolean) => void;
  setZoomMode: (mode: ZoomMode) => void;
  setCurrentFrame: (base64: string | null, index: number) => void;
  toggleZoomMode: () => void;
}

export const usePlaybackStore = create<PlaybackState>((set) => ({
  earliest: 0,
  latest: 0,
  count: 0,
  pinnedIndex: null,
  isPlaying: false,
  zoomMode: "fit",
  currentFrameBase64: null,
  currentFrameIndex: 0,

  setRange: (earliest, latest, count) => set({ earliest, latest, count }),
  setPinnedIndex: (index) => set({ pinnedIndex: index }),
  setPlaying: (playing) => set({ isPlaying: playing }),
  setZoomMode: (mode) => set({ zoomMode: mode }),
  setCurrentFrame: (base64, index) =>
    set({ currentFrameBase64: base64, currentFrameIndex: index }),
  toggleZoomMode: () =>
    set((s) => ({ zoomMode: s.zoomMode === "fit" ? "100%" : "fit" })),
}));
