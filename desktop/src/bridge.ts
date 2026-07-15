// Typed client for the Tauri command layer that wraps the Rust ↔ C++ bridge
// (mib-bridge, ADR 0003). Mirrors the DTOs in src-tauri/src/lib.rs.
import { invoke } from "@tauri-apps/api/core";

export interface CmdResult {
  ok: boolean;
  command: number;
  message: string;
}

export interface FrameMeta {
  valid: boolean;
  frame_index: number;
  timestamp_ns: number;
  width: number;
  height: number;
  pixel_format: number;
  stride_bytes: number;
  byte_len: number;
}

export interface BridgeEvent {
  kind: string;
  u0: number; u1: number; u2: number; u3: number; u4: number; u5: number;
  f0: number; f1: number; f2: number;
  b0: boolean; b1: boolean;
  text: string;
}

export const bridge = {
  abiVersion: () => invoke<number>("abi_version"),
  isInitialized: () => invoke<boolean>("is_initialized"),
  init: (dataDir: string) => invoke<boolean>("init", { dataDir }),
  configureMock: (frameDir: string, frameIntervalMs: number, loopFiles: boolean) =>
    invoke<CmdResult>("configure_mock", { frameDir, frameIntervalMs, loopFiles }),
  startCapture: () => invoke<CmdResult>("start_capture"),
  stopCapture: () => invoke<CmdResult>("stop_capture"),
  seekLatest: () => invoke<CmdResult>("seek_latest"),
  pollEvents: () => invoke<BridgeEvent[]>("poll_events"),
  fetchFrame: () => invoke<FrameMeta>("fetch_frame"),
  // Recording + review (bridge schema v2).
  startRecording: (filePath: string) => invoke<CmdResult>("start_recording", { filePath }),
  stopRecording: () => invoke<CmdResult>("stop_recording"),
  loadRecording: (filePath: string) => invoke<CmdResult>("load_recording", { filePath }),
  seekIndex: (frameIndex: number) => invoke<CmdResult>("seek_index", { frameIndex }),
  fetchFrameByIndex: (frameIndex: number) =>
    invoke<FrameMeta>("fetch_frame_by_index", { frameIndex }),
  // Binary IPC response — raw Mono8 bytes, never base64 (ADR 0003).
  frameBytes: async (): Promise<Uint8Array> => {
    const buf = await invoke<ArrayBuffer>("frame_bytes");
    return new Uint8Array(buf);
  },
};

// Expand a Mono8 buffer (with row stride) into an RGBA ImageData for a canvas.
export function mono8ToImageData(
  bytes: Uint8Array,
  width: number,
  height: number,
  stride: number,
): ImageData {
  const rgba = new Uint8ClampedArray(width * height * 4);
  const rowStride = stride > 0 ? stride : width;
  for (let y = 0; y < height; y++) {
    const src = y * rowStride;
    for (let x = 0; x < width; x++) {
      const g = bytes[src + x] ?? 0;
      const d = (y * width + x) * 4;
      rgba[d] = g;
      rgba[d + 1] = g;
      rgba[d + 2] = g;
      rgba[d + 3] = 255;
    }
  }
  return new ImageData(rgba, width, height);
}
