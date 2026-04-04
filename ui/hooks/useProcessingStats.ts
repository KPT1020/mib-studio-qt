import { useEffect } from "react";
import { listen } from "@tauri-apps/api/event";
import { useCaptureStore } from "../stores/captureStore";
import { useProcessingStore } from "../stores/processingStore";
import type { StatsUpdateEvent, BackgroundCapturedEvent } from "../types/events";

export function useProcessingStats() {
  const setStats = useCaptureStore((s) => s.setStats);
  const setProcessingStats = useProcessingStore((s) => s.setProcessingStats);
  const setBackgroundImage = useProcessingStore((s) => s.setBackgroundImage);

  useEffect(() => {
    const unlistenStats = listen<StatsUpdateEvent>("stats:update", (event) => {
      const p = event.payload;
      setStats(p.captureFrameRate, p.captureDataRateMBps, 0);
      setProcessingStats(p.algoFps, p.validFps, p.invalidFps, p.algoAvgUs, p.totalValidFlushed);
    });

    const unlistenBg = listen<BackgroundCapturedEvent>("background:captured", (event) => {
      setBackgroundImage(event.payload.imageBase64);
    });

    return () => {
      unlistenStats.then((fn) => fn());
      unlistenBg.then((fn) => fn());
    };
  }, [setStats, setProcessingStats, setBackgroundImage]);
}
