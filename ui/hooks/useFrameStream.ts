import { useEffect } from "react";
import { listen } from "@tauri-apps/api/event";
import { usePlaybackStore } from "../stores/playbackStore";
import type { FrameNewEvent } from "../types/events";

export function useFrameStream() {
  const setCurrentFrame = usePlaybackStore((s) => s.setCurrentFrame);

  useEffect(() => {
    let cancelled = false;
    const unlistenFns: Array<() => void> = [];

    const register = async () => {
      const fn = await listen<FrameNewEvent>("frame:new", (event) => {
        setCurrentFrame(event.payload.imageBase64, event.payload.index);
      });
      if (cancelled) {
        fn();
        return;
      }
      unlistenFns.push(fn);
    };

    register();

    return () => {
      cancelled = true;
      unlistenFns.forEach((fn) => fn());
    };
  }, [setCurrentFrame]);
}
