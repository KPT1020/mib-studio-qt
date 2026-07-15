import { useCallback, useEffect, useRef, useState } from "react";
import { open, save } from "@tauri-apps/plugin-dialog";
import {
  bridge,
  mono8ToImageData,
  type BridgeEvent,
  type FrameMeta,
  type ProcessingStats,
} from "./bridge";

const H5_FILTER = [{ name: "HDF5", extensions: ["h5"] }];

// Phase 3 + 4 slices: mock camera live view, frame recording to HDF5, and
// review (load a recording and scrub by frame index) — all through the Tauri
// bridge over the Qt-free C++ backend.
export default function App() {
  const [abi, setAbi] = useState<number | null>(null);
  const [ready, setReady] = useState(false);
  const [running, setRunning] = useState(false);
  const [frameDir, setFrameDir] = useState("");
  const [status, setStatus] = useState<string>("idle");
  const [log, setLog] = useState<string[]>([]);
  const [lastMeta, setLastMeta] = useState<string>("");

  // Recording.
  const [recording, setRecording] = useState(false);
  const [recPath, setRecPath] = useState("");

  // Processing.
  const [procEnabled, setProcEnabled] = useState(false);
  const [pixelToMicron, setPixelToMicron] = useState("1.0");
  const [stats, setStats] = useState<ProcessingStats | null>(null);

  // Review.
  const [reviewPath, setReviewPath] = useState("");
  const [reviewing, setReviewing] = useState(false);
  const [range, setRange] = useState({ earliest: 0, latest: 0, count: 0 });
  const [reviewIndex, setReviewIndex] = useState(0);

  const canvasRef = useRef<HTMLCanvasElement>(null);
  const loopRef = useRef<number | null>(null);

  const append = useCallback((line: string) => {
    setLog((l) => [line, ...l].slice(0, 12));
  }, []);

  useEffect(() => {
    bridge.abiVersion().then(setAbi).catch((e) => append(`abi error: ${e}`));
  }, [append]);

  // Draw a frame whose metadata is already known by pulling its pixel bytes.
  const draw = useCallback(async (meta: FrameMeta) => {
    if (!meta.valid) return;
    const bytes = await bridge.frameBytes();
    const canvas = canvasRef.current;
    if (!canvas) return;
    canvas.width = meta.width;
    canvas.height = meta.height;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    ctx.putImageData(mono8ToImageData(bytes, meta.width, meta.height, meta.stride_bytes), 0, 0);
    setLastMeta(
      `#${meta.frame_index} ${meta.width}×${meta.height} stride=${meta.stride_bytes} bytes=${meta.byte_len}`,
    );
  }, []);

  const browseFrameDir = useCallback(async () => {
    const picked = await open({ directory: true, title: "Select mock frame directory" });
    if (typeof picked === "string") setFrameDir(picked);
  }, []);

  const browseRecPath = useCallback(async () => {
    const picked = await save({ title: "Recording output", filters: H5_FILTER, defaultPath: "clip.h5" });
    if (picked) setRecPath(picked);
  }, []);

  const browseReviewPath = useCallback(async () => {
    const picked = await open({ title: "Open recording", filters: H5_FILTER, multiple: false });
    if (typeof picked === "string") setReviewPath(picked);
  }, []);

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
        setStatus(`${e.b1 ? "running" : e.b0 ? "configured" : "unconfigured"} (${e.text || "camera"})`);
      } else if (e.kind === "PlaybackPosition") {
        // u2 earliest, u3 latest, u4 available.
        setRange({ earliest: e.u2, latest: e.u3, count: e.u4 });
      } else if (e.kind === "BackendError") {
        append(`backend error: ${e.text}`);
      }
    }
  }, [append]);

  const tick = useCallback(async () => {
    try {
      applyEvents(await bridge.pollEvents());
      const meta = await bridge.fetchFrame();
      await draw(meta);
      if (procEnabled) setStats(await bridge.fetchProcessingStats());
    } catch (e) {
      append(`tick error: ${e}`);
    }
  }, [applyEvents, draw, append, procEnabled]);

  const onApplyProcessing = useCallback(async () => {
    try {
      const factor = Number(pixelToMicron) || 1.0;
      const res = await bridge.applyProcessing(procEnabled, factor);
      if (!res.ok) return append(`processing failed: ${res.message}`);
      append(`processing ${procEnabled ? "enabled" : "disabled"} (px→µm ${factor})`);
      setStats(await bridge.fetchProcessingStats());
    } catch (e) {
      append(`processing error: ${e}`);
    }
  }, [procEnabled, pixelToMicron, append]);

  const onStart = useCallback(async () => {
    try {
      setReviewing(false);
      const cfg = await bridge.configureMock(frameDir, 33, true);
      if (!cfg.ok) return append(`configure failed: ${cfg.message}`);
      const res = await bridge.startCapture();
      if (!res.ok) return append(`start failed: ${res.message}`);
      setRunning(true);
      append("capture started");
      loopRef.current = window.setInterval(tick, 100);
    } catch (e) {
      append(`start error: ${e}`);
    }
  }, [frameDir, tick, append]);

  const stopLoop = useCallback(() => {
    if (loopRef.current !== null) {
      window.clearInterval(loopRef.current);
      loopRef.current = null;
    }
  }, []);

  const onStop = useCallback(async () => {
    stopLoop();
    try {
      if (recording) {
        await bridge.stopRecording();
        setRecording(false);
      }
      await bridge.stopCapture();
      setRunning(false);
      append("capture stopped");
    } catch (e) {
      append(`stop error: ${e}`);
    }
  }, [recording, stopLoop, append]);

  const onToggleRecord = useCallback(async () => {
    try {
      if (!recording) {
        const res = await bridge.startRecording(recPath);
        if (!res.ok) return append(`record failed: ${res.message}`);
        setRecording(true);
        append(`recording → ${recPath}`);
      } else {
        await bridge.stopRecording();
        setRecording(false);
        append("recording stopped");
      }
    } catch (e) {
      append(`record error: ${e}`);
    }
  }, [recording, recPath, append]);

  const onLoadReview = useCallback(async () => {
    stopLoop();
    setRunning(false);
    try {
      const res = await bridge.loadRecording(reviewPath);
      if (!res.ok) return append(`load failed: ${res.message}`);
      setReviewing(true);
      append(`loaded ${reviewPath}`);
      // A PlaybackPosition event lands via poll; also seek to the first frame.
      applyEvents(await bridge.pollEvents());
      await onScrub(range.earliest);
    } catch (e) {
      append(`load error: ${e}`);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [reviewPath, append, applyEvents, range.earliest, stopLoop]);

  const onScrub = useCallback(async (idx: number) => {
    setReviewIndex(idx);
    try {
      await bridge.seekIndex(idx);
      const meta = await bridge.fetchFrameByIndex(idx);
      await draw(meta);
      applyEvents(await bridge.pollEvents());
    } catch (e) {
      append(`seek error: ${e}`);
    }
  }, [draw, applyEvents, append]);

  useEffect(() => () => stopLoop(), [stopLoop]);

  return (
    <main style={{ fontFamily: "system-ui, sans-serif", padding: 24, maxWidth: 900, margin: "0 auto" }}>
      <h1>MIB Studio <span style={{ fontWeight: 400, fontSize: 16, opacity: 0.6 }}>React + Tauri</span></h1>
      <p style={{ opacity: 0.7 }}>
        Bridge ABI: {abi ?? "…"} · backend: {ready ? "ready" : "not initialized"} · camera: {status}
        {reviewing ? " · reviewing" : ""}
      </p>

      <div style={{ display: "flex", gap: 8, alignItems: "center", flexWrap: "wrap", margin: "12px 0" }}>
        <button onClick={onInit} disabled={ready}>Initialize backend</button>
        <input
          style={{ flex: 1, minWidth: 220, padding: 6 }}
          placeholder="mock frame directory (folder of .tiff/.png)"
          value={frameDir}
          onChange={(e) => setFrameDir(e.target.value)}
        />
        <button onClick={browseFrameDir}>Browse…</button>
        <button onClick={onStart} disabled={!ready || running || !frameDir}>Start mock capture</button>
        <button onClick={onStop} disabled={!running}>Stop</button>
      </div>

      <fieldset style={{ margin: "8px 0", padding: 12 }}>
        <legend>Recording</legend>
        <div style={{ display: "flex", gap: 8, alignItems: "center", flexWrap: "wrap" }}>
          <input
            style={{ flex: 1, minWidth: 220, padding: 6 }}
            placeholder="recording output path (…/clip.h5)"
            value={recPath}
            onChange={(e) => setRecPath(e.target.value)}
          />
          <button onClick={browseRecPath} disabled={recording}>Browse…</button>
          <button onClick={onToggleRecord} disabled={!running || !recPath}>
            {recording ? "Stop recording" : "Record"}
          </button>
        </div>
      </fieldset>

      <fieldset style={{ margin: "8px 0", padding: 12 }}>
        <legend>Processing</legend>
        <div style={{ display: "flex", gap: 12, alignItems: "center", flexWrap: "wrap" }}>
          <label>
            <input
              type="checkbox"
              checked={procEnabled}
              onChange={(e) => setProcEnabled(e.target.checked)}
            /> realtime processing
          </label>
          <label>
            px→µm{" "}
            <input
              style={{ width: 80, padding: 4 }}
              value={pixelToMicron}
              onChange={(e) => setPixelToMicron(e.target.value)}
            />
          </label>
          <button onClick={onApplyProcessing} disabled={!ready}>Apply</button>
          {stats?.valid && (
            <span style={{ fontFamily: "monospace", fontSize: 12 }}>
              algo {stats.algo_fps1s.toFixed(1)} · valid {stats.valid_fps1s.toFixed(1)} ·
              invalid {stats.invalid_fps1s.toFixed(1)} fps · px→µm {stats.pixel_to_micron}
            </span>
          )}
        </div>
      </fieldset>

      <fieldset style={{ margin: "8px 0", padding: 12 }}>
        <legend>Review</legend>
        <div style={{ display: "flex", gap: 8, alignItems: "center", flexWrap: "wrap" }}>
          <input
            style={{ flex: 1, minWidth: 220, padding: 6 }}
            placeholder="recording to review (…/clip.h5)"
            value={reviewPath}
            onChange={(e) => setReviewPath(e.target.value)}
          />
          <button onClick={browseReviewPath}>Browse…</button>
          <button onClick={onLoadReview} disabled={!ready || !reviewPath}>Load recording</button>
        </div>
        {reviewing && range.count > 0 && (
          <div style={{ marginTop: 8 }}>
            <input
              type="range"
              min={range.earliest}
              max={range.latest}
              value={reviewIndex}
              onChange={(e) => onScrub(Number(e.target.value))}
              style={{ width: "100%" }}
            />
            <span style={{ fontFamily: "monospace", fontSize: 12 }}>
              frame {reviewIndex} of [{range.earliest}…{range.latest}] ({range.count} available)
            </span>
          </div>
        )}
      </fieldset>

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
