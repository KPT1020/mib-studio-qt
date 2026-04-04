import { useCallback } from "react";
import { usePlaybackStore } from "../stores/playbackStore";
import { fetchFrameByIndex, fetchLatestFrame, getPlaybackRange } from "./useBackend";

export function usePlayback() {
  const { pinnedIndex, setPinnedIndex, setRange, setCurrentFrame } =
    usePlaybackStore();

  const refreshRange = useCallback(async () => {
    try {
      const range = await getPlaybackRange();
      setRange(range.earliest, range.latest, range.count);
    } catch {
      // Backend not connected yet
    }
  }, [setRange]);

  const seekToIndex = useCallback(
    async (index: number) => {
      setPinnedIndex(index);
      try {
        const frame = await fetchFrameByIndex(index);
        if (frame) {
          setCurrentFrame(frame.imageBase64, frame.index);
        }
      } catch {
        // Ignore errors during seek
      }
    },
    [setPinnedIndex, setCurrentFrame]
  );

  const seekToLatest = useCallback(async () => {
    setPinnedIndex(null);
    try {
      const frame = await fetchLatestFrame();
      if (frame) {
        setCurrentFrame(frame.imageBase64, frame.index);
      }
    } catch {
      // Ignore
    }
  }, [setPinnedIndex, setCurrentFrame]);

  return { pinnedIndex, refreshRange, seekToIndex, seekToLatest };
}
