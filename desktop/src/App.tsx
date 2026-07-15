import { useCallback, useEffect, useRef, useState } from "react";
import { bridge, mono8ToImageData, type BridgeEvent } from "./bridge";

// Phase 3 vertical slice: mock camera end to end. Init the Qt-free C++ backend
// through the Tauri bridge, configure a mock camera, start capture, and render
// live Mono8 frames to a canvas while draining status events.
export default function App() {
  const [abi, setAbi] = useState<number | null>(null);
  const [ready, setReady] = useState(false);
  const [running, setRunning] = useState(false);
  const [frameDir, setFrameDir] = useState("");
  const [status, setStatus] = useState<string>("idle");
  const [log, setLog] = useState<string[]>([]);
  const [lastMeta, setLastMeta] = useState<string>("");
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const loopRef = useRef<number | null>(null);

  const append = useCallback((line: string) => {
    setLog((l) => [line, ...l].slice(0, 12));
  }, []);

  useEffect(() => {
    bridge.abiVersion().then(setAbi).catch((e) => append(`abi error: ${e}`));
  }, [append]);

  const onInit = useCallback(async () => {
    try {
      const ok = await bridge.init("");
      setReady(ok);
      append(ok ? "backend initialized" : "backend init failed");
    } catch (e) {
      append(`init error: ${e}`);
    }
  }, [append]);

  const applyEvents = useCallback((events: BridgeEvent[]) => {
    for (const e of events) {
      if (e.kind === "CameraStatus") {
        setStatus(
          `${e.b1 ? "running" : e.b0 ? "configured" : "unconfigured"} ` +
            `(${e.text || "camera"})`,
        );
      } else if (e.kind === "BackendError") {
        append(`backend error: ${e.text}`);
      }
    }
  }, [append]);

  const renderFrame = useCallback(async () => {
    const meta = await bridge.fetchFrame();
    if (!meta.valid) return;
    const bytes = await bridge.frameBytes();
    const canvas = canvasRef.current;
    if (!canvas) return;
    canvas.width = meta.width;
    canvas.height = meta.height;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    ctx.putImageData(
      mono8ToImageData(bytes, meta.width, meta.height, meta.stride_bytes),
      0,
      0,
    );
    setLastMeta(
      `#${meta.frame_index} ${meta.width}×${meta.height} ` +
        `stride=${meta.stride_bytes} bytes=${meta.byte_len}`,
    );
  }, []);

  const tick = useCallback(async () => {
    try {
      applyEvents(await bridge.pollEvents());
      await renderFrame();
    } catch (e) {
      append(`tick error: ${e}`);
    }
  }, [applyEvents, renderFrame, append]);

  const onStart = useCallback(async () => {
    try {
      const cfg = await bridge.configureMock(frameDir, 33, true);
      if (!cfg.ok) {
        append(`configure failed: ${cfg.message}`);
        return;
      }
      const res = await bridge.startCapture();
      if (!res.ok) {
        append(`start failed: ${res.message}`);
        return;
      }
      setRunning(true);
      append("capture started");
      loopRef.current = window.setInterval(tick, 100);
    } catch (e) {
      append(`start error: ${e}`);
    }
  }, [frameDir, tick, append]);

  const onStop = useCallback(async () => {
    if (loopRef.current !== null) {
      window.clearInterval(loopRef.current);
      loopRef.current = null;
    }
    try {
      await bridge.stopCapture();
      setRunning(false);
      append("capture stopped");
    } catch (e) {
      append(`stop error: ${e}`);
    }
  }, [append]);

  useEffect(() => () => {
    if (loopRef.current !== null) window.clearInterval(loopRef.current);
  }, []);

  return (
    <main style={{ fontFamily: "system-ui, sans-serif", padding: 24, maxWidth: 900, margin: "0 auto" }}>
      <h1>MIB Studio <span style={{ fontWeight: 400, fontSize: 16, opacity: 0.6 }}>React + Tauri (Phase 3)</span></h1>
      <p style={{ opacity: 0.7 }}>
        Bridge ABI: {abi ?? "…"} · backend: {ready ? "ready" : "not initialized"} · camera: {status}
      </p>

      <div style={{ display: "flex", gap: 8, alignItems: "center", flexWrap: "wrap", margin: "12px 0" }}>
        <button onClick={onInit} disabled={ready}>Initialize backend</button>
        <input
          style={{ flex: 1, minWidth: 260, padding: 6 }}
          placeholder="mock frame directory (folder of .tiff/.png)"
          value={frameDir}
          onChange={(e) => setFrameDir(e.target.value)}
        />
        <button onClick={onStart} disabled={!ready || running || !frameDir}>Start mock capture</button>
        <button onClick={onStop} disabled={!running}>Stop</button>
      </div>

      <canvas
        ref={canvasRef}
        style={{ width: "100%", maxWidth: 640, border: "1px solid #ccc", imageRendering: "pixelated", background: "#111" }}
      />
      <p style={{ fontFamily: "monospace", fontSize: 13 }}>{lastMeta || "no frame yet"}</p>

      <h3>Log</h3>
      <ul style={{ fontFamily: "monospace", fontSize: 12, lineHeight: 1.5 }}>
        {log.map((l, i) => <li key={i}>{l}</li>)}
      </ul>
    </main>
  );
}
