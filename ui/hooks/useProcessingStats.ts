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
    let cancelled = false;
    const unlistenFns: Array<() => void> = [];

    const register = async () => {
      const statsFn = await listen<StatsUpdateEvent>("stats:update", (event) => {
        const p = event.payload;
        setStats(p.captureFrameRate, p.captureDataRateMbps, 0);
        setProcessingStats(p.algoFps, p.validFps, p.invalidFps, p.algoAvgUs, p.totalValidFlushed);
      });
      if (cancelled) {
        statsFn();
      } else {
        unlistenFns.push(statsFn);
      }

      const bgFn = await listen<BackgroundCapturedEvent>("background:captured", (event) => {
        setBackgroundImage(event.payload.imageBase64);
      });
      if (cancelled) {
        bgFn();
      } else {
        unlistenFns.push(bgFn);
      }
    };

    register();

    return () => {
      cancelled = true;
      unlistenFns.forEach((fn) => fn());
    };
  }, [setStats, setProcessingStats, setBackgroundImage]);
}
