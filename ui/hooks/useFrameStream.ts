import { useEffect } from "react";
import { listen } from "@tauri-apps/api/event";
import { usePlaybackStore } from "../stores/playbackStore";
import type { FrameNewEvent } from "../types/events";

export function useFrameStream() {
  const setCurrentFrame = usePlaybackStore((s) => s.setCurrentFrame);

  useEffect(() => {
    const unlisten = listen<FrameNewEvent>("frame:new", (event) => {
      setCurrentFrame(event.payload.imageBase64, event.payload.index);
    });

    return () => {
      unlisten.then((fn) => fn());
    };
  }, [setCurrentFrame]);
}
