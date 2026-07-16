import { useCallback, useEffect, useRef, useState } from "react";
import { open, save } from "@tauri-apps/plugin-dialog";
import {
  bridge,
  mono8ToImageData,
  type BridgeEvent,
  type FrameMeta,
  type ProcessingStats,
} from "./bridge";
import { BRIDGE_ABI_VERSION } from "./bridgeContract";
import "./App.css";

const H5_FILTER = [{ name: "HDF5", extensions: ["h5"] }];
const SIDEBAR_KEY = "mib.sidebar.collapsed";

// Standard reason strings for controls whose backend surface is not bridged
// yet. Shown as tooltips — the control stays visible (Qt parity) but cannot
// be activated, and never fakes backend or hardware state.
const PENDING = {
  discovery: "Device discovery is not bridged yet — backend issue BE-2 (#272)",
  roi: "ROI editing is not bridged yet — backend issue BE-3 (#273)",
  script: "Camera script/config apply is not bridged yet — BE-2 (#272) / BE-3 (#273)",
  config: "App config / profiles are not bridged yet — backend issue BE-3 (#273)",
  experiment: "Experiment orchestration is not bridged yet — backend issue BE-4 (#274)",
  monitoring: "Monitoring data is not bridged yet — backend issue BE-5 (#275)",
  review: "HDF5 metadata/metrics/export are not bridged yet — backend issue BE-6 (#276)",
  autofocus: "Autofocus/nanopositioner control is not bridged yet — BE-8 (#278)",
  platform: "Platform/shell services are not migrated yet — BE-9 (#279)",
  background: "Background image control is not bridged yet — backend issue BE-3 (#273)",
};

type MainTab = "connect" | "overview" | "experiment" | "review";

interface RatesRef {
  windowStartMs: number;
  frames: number;
  bytes: number;
  displayFps: number;
  dataRateMBs: number;
  lastFrameIndex: number;
}

function SideRow(props: { k: string; v: string; cls?: string }) {
  return (
    <div className="side-row">
      <span className="k">{props.k}</span>
      <span className={`v ${props.cls ?? ""}`}>{props.v}</span>
    </div>
  );
}

// A menu-bar dropdown. Items with a `pending` reason render disabled with the
// reason as tooltip.
function Menu(props: {
  label: string;
  items: { label: string; onClick?: () => void; pending?: string }[];
}) {
  const [openMenu, setOpenMenu] = useState(false);
  return (
    <div className="menubar-item">
      <button aria-expanded={openMenu} onClick={() => setOpenMenu((o) => !o)} onBlur={() => window.setTimeout(() => setOpenMenu(false), 150)}>
        {props.label}
      </button>
      {openMenu && (
        <div className="menu-popup" role="menu">
          {props.items.map((it) => (
            <button
              key={it.label}
              role="menuitem"
              disabled={!!it.pending}
              title={it.pending}
              onClick={() => {
                setOpenMenu(false);
                it.onClick?.();
              }}
            >
              {it.label}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

// Operator shell aligned with the Qt application on main (issue #266):
// menu row, collapsible telemetry sidebar, Connect / Overview / Experiment /
// Review tabs with camera actions in the header, and a metrics status bar.
// All implemented bridge schema-v3 actions stay wired; everything else is
// visible but disabled with an explanatory tooltip.
export default function App() {
  const [abi, setAbi] = useState<number | null>(null);
  const [ready, setReady] = useState(false);
  const [running, setRunning] = useState(false);
  const [camStatus, setCamStatus] = useState("unconfigured");
  const [log, setLog] = useState<string[]>([]);
  const [showLog, setShowLog] = useState(false);
  const [lastMeta, setLastMeta] = useState<FrameMeta | null>(null);

  // Shell state.
  const [tab, setTab] = useState<MainTab>("connect");
  const [connectTab, setConnectTab] = useState<"cameras" | "mindvision" | "framegrabbers">("cameras");
  const [expTab, setExpTab] = useState<"preview" | "monitoring">("preview");
  const [configTab, setConfigTab] = useState<"app" | "script">("app");
  const [sidebarCollapsed, setSidebarCollapsed] = useState(
    () => localStorage.getItem(SIDEBAR_KEY) === "1",
  );
  const [fitWindow, setFitWindow] = useState(true);
  const [showAbout, setShowAbout] = useState(false);

  // Mock camera configuration (the only bridged source today).
  const [showMockConfig, setShowMockConfig] = useState(false);
  const [frameDir, setFrameDir] = useState("");
  const [intervalMs, setIntervalMs] = useState("33");
  const [loopFiles, setLoopFiles] = useState(true);
  const [mockConfigured, setMockConfigured] = useState(false);

  // Recording.
  const [recording, setRecording] = useState(false);
  const [recPath, setRecPath] = useState("");

  // Processing (bridge schema v3).
  const [procEnabled, setProcEnabled] = useState(false);
  const [pixelToMicron, setPixelToMicron] = useState("1.0");
  const [stats, setStats] = useState<ProcessingStats | null>(null);

  // Review.
  const [reviewPath, setReviewPath] = useState("");
  const [reviewing, setReviewing] = useState(false);
  const [reviewTab, setReviewTab] = useState<"raw" | "valid" | "invalid" | "charts">("raw");
  const [range, setRange] = useState({ earliest: 0, latest: 0, count: 0 });
  const [reviewIndex, setReviewIndex] = useState(0);

  const liveCanvasRef = useRef<HTMLCanvasElement>(null);
  const previewCanvasRef = useRef<HTMLCanvasElement>(null);
  const reviewCanvasRef = useRef<HTMLCanvasElement>(null);
  const loopRef = useRef<number | null>(null);
  const tabRef = useRef<MainTab>("connect");
  tabRef.current = tab;

  // Display-side measurements (frames actually drawn / bytes actually pulled
  // over the last 1s window). These are UI measurements, not backend claims.
  const ratesRef = useRef<RatesRef>({
    windowStartMs: performance.now(),
    frames: 0,
    bytes: 0,
    displayFps: 0,
    dataRateMBs: 0,
    lastFrameIndex: -1,
  });
  const [, setRatesTick] = useState(0);

  const append = useCallback((line: string) => {
    setLog((l) => [`${new Date().toLocaleTimeString()} ${line}`, ...l].slice(0, 50));
  }, []);

  // Initialize the backend on boot (empty data dir resolves to Tauri's
  // app_data_dir on the Rust side) — the Qt app has no manual init step.
  useEffect(() => {
    bridge
      .abiVersion()
      .then((v) => {
        setAbi(v);
        if (v < BRIDGE_ABI_VERSION) {
          append(`bridge ABI ${v} is older than the UI contract (${BRIDGE_ABI_VERSION})`);
        }
      })
      .catch((e) => append(`abi error: ${e}`));
    (async () => {
      try {
        const already = await bridge.isInitialized();
        const ok = already || (await bridge.init(""));
        setReady(ok);
        append(ok ? "backend initialized" : "backend init failed");
      } catch (e) {
        append(`init error: ${e}`);
      }
    })();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const activeLiveCanvas = useCallback((): HTMLCanvasElement | null => {
    if (tabRef.current === "experiment") return previewCanvasRef.current;
    return liveCanvasRef.current;
  }, []);

  // Draw a frame whose metadata is already known by pulling its pixel bytes.
  const draw = useCallback(
    async (meta: FrameMeta, canvas: HTMLCanvasElement | null) => {
      if (!meta.valid || !canvas) return;
      if (meta.frame_index === ratesRef.current.lastFrameIndex && canvas !== reviewCanvasRef.current) return;
      const bytes = await bridge.frameBytes();
      canvas.width = meta.width;
      canvas.height = meta.height;
      const ctx = canvas.getContext("2d");
      if (!ctx) return;
      ctx.putImageData(mono8ToImageData(bytes, meta.width, meta.height, meta.stride_bytes), 0, 0);
      setLastMeta(meta);
      const r = ratesRef.current;
      r.frames += 1;
      r.bytes += meta.byte_len;
      r.lastFrameIndex = meta.frame_index;
      const now = performance.now();
      const elapsed = now - r.windowStartMs;
      if (elapsed >= 1000) {
        r.displayFps = (r.frames * 1000) / elapsed;
        r.dataRateMBs = (r.bytes * 1000) / elapsed / (1024 * 1024);
        r.frames = 0;
        r.bytes = 0;
        r.windowStartMs = now;
        setRatesTick((t) => t + 1);
      }
    },
    [],
  );

  const applyEvents = useCallback(
    (events: BridgeEvent[]) => {
      for (const e of events) {
        if (e.kind === "CameraStatus") {
          setCamStatus(`${e.b1 ? "running" : e.b0 ? "configured" : "unconfigured"} (${e.text || "camera"})`);
        } else if (e.kind === "PlaybackPosition") {
          // u2 earliest, u3 latest, u4 available.
          setRange({ earliest: e.u2, latest: e.u3, count: e.u4 });
        } else if (e.kind === "BackendError") {
          append(`backend error: ${e.text}`);
        } else if (e.kind === "OperationStatus") {
          // u0 id, u2 state (2 Completed, 3 Failed, 4 Cancelled, 5 TimedOut).
          if (e.u2 >= 3) append(`operation ${e.u0} ${e.u2 === 3 ? "failed" : e.u2 === 4 ? "cancelled" : "timed out"}: ${e.text}`);
        } else if (e.kind === "QueueOverflow") {
          append(`event queue overflow: ${e.u0} coalesced (total ${e.u1})`);
        }
        // Kinds this build does not know (additive, newer bridge) fall through
        // and are ignored — never fatal (ADR 0004).
      }
    },
    [append],
  );

  const procEnabledRef = useRef(procEnabled);
  procEnabledRef.current = procEnabled;

  const tick = useCallback(async () => {
    try {
      applyEvents(await bridge.pollEvents());
      const meta = await bridge.fetchFrame();
      await draw(meta, activeLiveCanvas());
      if (procEnabledRef.current) setStats(await bridge.fetchProcessingStats());
    } catch (e) {
      append(`tick error: ${e}`);
    }
  }, [applyEvents, draw, append, activeLiveCanvas]);

  const stopLoop = useCallback(() => {
    if (loopRef.current !== null) {
      window.clearInterval(loopRef.current);
      loopRef.current = null;
    }
  }, []);

  useEffect(() => () => stopLoop(), [stopLoop]);

  const toggleSidebar = useCallback(() => {
    setSidebarCollapsed((c) => {
      localStorage.setItem(SIDEBAR_KEY, c ? "0" : "1");
      return !c;
    });
  }, []);

  // ---- Camera actions (main tab header) ----

  const onConfigureMock = useCallback(async () => {
    try {
      const cfg = await bridge.configureMock(frameDir, Number(intervalMs) || 33, loopFiles);
      if (!cfg.ok) return append(`configure failed: ${cfg.message}`);
      setMockConfigured(true);
      setShowMockConfig(false);
      setCamStatus("configured (mock)");
      append(`mock camera configured (${frameDir})`);
    } catch (e) {
      append(`configure error: ${e}`);
    }
  }, [frameDir, intervalMs, loopFiles, append]);

  const onStartCamera = useCallback(async () => {
    try {
      setReviewing(false);
      const res = await bridge.startCapture();
      if (!res.ok) return append(`start failed: ${res.message}`);
      setRunning(true);
      append("capture started");
      stopLoop();
      loopRef.current = window.setInterval(tick, 100);
      // Parity with Qt: a successful start lands the operator on Overview.
      setTab((t) => (t === "connect" ? "overview" : t));
    } catch (e) {
      append(`start error: ${e}`);
    }
  }, [tick, append, stopLoop]);

  const onStopCamera = useCallback(async () => {
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

  // ---- Recording (Experiment ▸ Preview toolbar) ----

  const onToggleRecord = useCallback(async () => {
    try {
      if (!recording) {
        const picked = await save({ title: "Recording output", filters: H5_FILTER, defaultPath: recPath || "clip.h5" });
        if (!picked) return;
        setRecPath(picked);
        const res = await bridge.startRecording(picked);
        if (!res.ok) return append(`record failed: ${res.message}`);
        setRecording(true);
        append(`recording → ${picked}`);
      } else {
        await bridge.stopRecording();
        setRecording(false);
        append("recording stopped");
      }
    } catch (e) {
      append(`record error: ${e}`);
    }
  }, [recording, recPath, append]);

  // ---- Processing settings (bridged subset of App config) ----

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

  // ---- Review ----

  const onScrub = useCallback(
    async (idx: number) => {
      setReviewIndex(idx);
      try {
        await bridge.seekIndex(idx);
        const meta = await bridge.fetchFrameByIndex(idx);
        await draw(meta, reviewCanvasRef.current);
        applyEvents(await bridge.pollEvents());
      } catch (e) {
        append(`seek error: ${e}`);
      }
    },
    [draw, applyEvents, append],
  );

  const onSelectHdf = useCallback(async () => {
    const picked = await open({ title: "Open recording", filters: H5_FILTER, multiple: false });
    if (typeof picked !== "string") return;
    stopLoop();
    setRunning(false);
    setReviewPath(picked);
    try {
      const res = await bridge.loadRecording(picked);
      if (!res.ok) return append(`load failed: ${res.message}`);
      setReviewing(true);
      append(`loaded ${picked}`);
      applyEvents(await bridge.pollEvents());
      await onScrub(range.earliest);
    } catch (e) {
      append(`load error: ${e}`);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [append, applyEvents, range.earliest, stopLoop, onScrub]);

  const openReviewFromMenu = useCallback(() => {
    setTab("review");
    void onSelectHdf();
  }, [onSelectHdf]);

  const r = ratesRef.current;
  const displayFps = running ? r.displayFps : 0;
  const dataRate = running ? r.dataRateMBs : 0;
  const algoFps = stats?.valid ? stats.algo_fps1s : 0;
  const validFps = stats?.valid ? stats.valid_fps1s : 0;
  const invalidFps = stats?.valid ? stats.invalid_fps1s : 0;

  const startCameraReason = !ready
    ? "Backend is not initialized"
    : !mockConfigured
      ? "No camera configured — use Connect ▸ Configure Mock… (hardware discovery pending BE-2 #272)"
      : running
        ? "Camera is already running"
        : undefined;

  return (
    <div className="app">
      {/* ---- Menu row ---- */}
      <nav className="menubar" aria-label="Main menu">
        <Menu
          label="File"
          items={[
            { label: "Open Recording…", onClick: openReviewFromMenu },
            { label: "Open Data Folder", pending: PENDING.platform },
            { label: "Exit", pending: PENDING.platform },
          ]}
        />
        <Menu
          label="Settings"
          items={[
            { label: "Processing Settings…", pending: PENDING.config },
            { label: "Pixel to Micron…", pending: PENDING.config },
            { label: "Monitoring Settings…", pending: PENDING.monitoring },
            { label: "Updates…", pending: PENDING.platform },
          ]}
        />
        <Menu
          label="Help"
          items={[
            { label: "About", onClick: () => setShowAbout(true) },
            { label: "Documentation", pending: PENDING.platform },
            { label: "Report a Problem…", pending: PENDING.platform },
          ]}
        />
      </nav>

      <div className="body">
        {/* ---- Telemetry sidebar ---- */}
        <aside className={`sidebar ${sidebarCollapsed ? "collapsed" : ""}`} aria-label="Telemetry sidebar">
          <div className="side-section">
            <div className="bg-preview" title={PENDING.background}>
              No background set
            </div>
          </div>
          <div className="side-section">
            <h4>Display</h4>
            <SideRow k="FPS:" v={displayFps.toFixed(1)} />
          </div>
          <div className="side-section">
            <h4>Processing</h4>
            <SideRow k="Algo FPS:" v={stats?.valid ? algoFps.toFixed(1) : "—"} cls={stats?.valid ? "" : "dim"} />
            <SideRow k="Valid FPS:" v={stats?.valid ? validFps.toFixed(1) : "—"} cls={stats?.valid ? "" : "dim"} />
            <SideRow k="Invalid FPS:" v={stats?.valid ? invalidFps.toFixed(1) : "—"} cls={stats?.valid ? "" : "dim"} />
            <SideRow k="px→µm:" v={stats?.valid ? String(stats.pixel_to_micron) : "—"} cls={stats?.valid ? "" : "dim"} />
          </div>
          <div className="side-section">
            <h4>Camera</h4>
            <SideRow k="Status:" v={running ? "Running" : camStatus} cls={running ? "ok" : ""} />
            <SideRow k="Display rate:" v={`${displayFps.toFixed(1)} fps`} />
            <SideRow k="Data rate:" v={`${dataRate.toFixed(1)} MB/s`} />
          </div>
          <div className="side-section" title={PENDING.autofocus}>
            <h4>Autofocus</h4>
            <SideRow k="Ring width:" v="—" cls="dim" />
          </div>
          <div className="side-section" title={PENDING.experiment}>
            <h4>Experiment</h4>
            <SideRow k="Status:" v="Not migrated" cls="dim" />
            <SideRow k="Valid Buffered:" v="—" cls="dim" />
            <SideRow k="Invalid Buffered:" v="—" cls="dim" />
            <SideRow k="Flush Status:" v="—" cls="dim" />
            <SideRow k="Runtime:" v="—" cls="dim" />
          </div>
          <div className="side-section" title={PENDING.autofocus}>
            <h4>Nanopositioner Autofocus</h4>
            <SideRow k="COM Port:" v="—" cls="dim" />
            <SideRow k="Baud Rate:" v="—" cls="dim" />
            <SideRow k="Device Address:" v="—" cls="dim" />
          </div>
        </aside>
        <button
          className="sidebar-toggle"
          onClick={toggleSidebar}
          title={sidebarCollapsed ? "Show telemetry sidebar" : "Hide telemetry sidebar"}
          aria-label={sidebarCollapsed ? "Show telemetry sidebar" : "Hide telemetry sidebar"}
        >
          {sidebarCollapsed ? "▶" : "◀"}
        </button>

        {/* ---- Main tabbed area ---- */}
        <main className="main">
          <div className="tabs-header">
            <div className="tabbar" role="tablist" aria-label="Workflow tabs">
              {(["connect", "overview", "experiment", "review"] as MainTab[]).map((t) => (
                <button
                  key={t}
                  role="tab"
                  aria-selected={tab === t}
                  className={tab === t ? "active" : ""}
                  onClick={() => setTab(t)}
                >
                  {t[0].toUpperCase() + t.slice(1)}
                </button>
              ))}
            </div>
            <div className="spacer" />
            <div className="camera-actions">
              <button onClick={onStartCamera} disabled={!!startCameraReason} title={startCameraReason}>
                Start Camera
              </button>
              <button onClick={onStopCamera} disabled={!running} title={running ? undefined : "Camera is not running"}>
                Stop Camera
              </button>
            </div>
          </div>

          <div className="tab-body">
            {/* ---- Connect ---- */}
            {tab === "connect" && (
              <>
                <p style={{ margin: "0 0 6px" }}>Available devices:</p>
                <div className="subtabs" role="tablist" aria-label="Device sources">
                  <button className={connectTab === "cameras" ? "active" : ""} onClick={() => setConnectTab("cameras")}>
                    Cameras
                  </button>
                  <button className={connectTab === "mindvision" ? "active" : ""} onClick={() => setConnectTab("mindvision")}>
                    MindVision
                  </button>
                  <button className={connectTab === "framegrabbers" ? "active" : ""} onClick={() => setConnectTab("framegrabbers")}>
                    Framegrabbers
                  </button>
                </div>
                <div className="subtab-body">
                  <div className="devices-list">
                    {connectTab === "cameras" && "No EGrabber camera enumeration available — pending BE-2 (#272)."}
                    {connectTab === "mindvision" && "No MindVision enumeration available — pending BE-2 (#272)."}
                    {connectTab === "framegrabbers" && "No framegrabber enumeration available — pending BE-2 (#272)."}
                  </div>
                  <div className="toolbar" style={{ marginTop: 8 }}>
                    <button disabled title={PENDING.discovery}>Refresh</button>
                    <button disabled title={PENDING.discovery}>Connect</button>
                    <div className="right">
                      <button className="btn" onClick={() => setShowMockConfig(true)} disabled={!ready} title={ready ? undefined : "Backend is not initialized"}>
                        Configure Mock…
                      </button>
                    </div>
                  </div>
                  <p className="path-label">
                    {mockConfigured
                      ? `Mock camera configured: ${frameDir}`
                      : "Hardware device discovery is not bridged yet (BE-2 #272) — the mock source is the only connectable camera."}
                  </p>
                </div>
              </>
            )}

            {/* ---- Overview ---- */}
            {tab === "overview" && (
              <>
                <div className="toolbar">
                  <button onClick={() => setFitWindow((f) => !f)}>{fitWindow ? "Fit: Window" : "Fit: 1:1"}</button>
                  <button disabled title={PENDING.roi}>ROI Overlay: Off</button>
                  <label>
                    W: <input type="number" disabled title={PENDING.roi} placeholder="—" /> px
                  </label>
                  <label>
                    H: <input type="number" disabled title={PENDING.roi} placeholder="—" /> px
                  </label>
                </div>
                <div className="canvas-wrap">
                  {!lastMeta && <span className="canvas-hint">No frame yet — configure a camera and press Start Camera</span>}
                  <canvas ref={liveCanvasRef} className={fitWindow ? "fit" : ""} />
                </div>
                <div className="toolbar" style={{ marginTop: 6 }}>
                  <button disabled title={PENDING.script}>Reset</button>
                  <button disabled title={PENDING.script}>Save</button>
                  <button disabled title={PENDING.script}>Apply to Camera</button>
                  <button disabled title={PENDING.script}>Browse…</button>
                  <button disabled title={PENDING.script}>Clear</button>
                  <span className="path-label right">camera script: not bridged (BE-2 #272)</span>
                </div>
                <textarea
                  className="script-editor"
                  disabled
                  title={PENDING.script}
                  value={"// Camera script editing is not bridged yet — BE-2 (#272)."}
                  readOnly
                />
                <p className="mono">
                  {lastMeta
                    ? `#${lastMeta.frame_index} ${lastMeta.width}×${lastMeta.height} stride=${lastMeta.stride_bytes} bytes=${lastMeta.byte_len}`
                    : "no frame yet"}
                </p>
              </>
            )}

            {/* ---- Experiment ---- */}
            {tab === "experiment" && (
              <>
                <div className="toolbar">
                  <div className="subtabs" role="tablist" aria-label="Experiment views">
                    <button className={expTab === "preview" ? "active" : ""} onClick={() => setExpTab("preview")}>
                      Preview
                    </button>
                    <button className={expTab === "monitoring" ? "active" : ""} onClick={() => setExpTab("monitoring")}>
                      Monitoring
                    </button>
                  </div>
                  <div className="right">
                    <span className="mono" title={PENDING.roi}>ROI: — (not bridged)</span>
                    <button disabled title={PENDING.experiment}>Start Experiment</button>
                    <button disabled title={PENDING.experiment}>Stop Experiment</button>
                  </div>
                </div>

                {expTab === "preview" && (
                  <>
                    <div className="canvas-wrap">
                      {!lastMeta && <span className="canvas-hint">No frame yet — configure a camera and press Start Camera</span>}
                      <canvas ref={previewCanvasRef} className={fitWindow ? "fit" : ""} />
                    </div>
                    <div className="toolbar" style={{ marginTop: 6 }}>
                      <button disabled title={PENDING.monitoring}>Overlay: Both</button>
                      <span className="legend">
                        <span className="chip"><span className="swatch" style={{ background: "#2b6cb0" }} /> Target</span>
                        <span className="chip"><span className="swatch" style={{ background: "#1a7f37" }} /> Valid</span>
                        <span className="chip"><span className="swatch" style={{ background: "#b42318" }} /> Invalid</span>
                      </span>
                      <button disabled title={PENDING.background}>Set Background</button>
                      <label title={PENDING.background}>
                        <input type="checkbox" disabled /> Auto
                      </label>
                      <button disabled title={PENDING.roi}>Clear ROI</button>
                      <button disabled title={PENDING.experiment}>Save Buffer</button>
                      <button onClick={onToggleRecord} disabled={!running} title={running ? "Record raw frames to an HDF5 file" : "Camera is not running"}>
                        {recording ? "Stop Recording" : "Record"}
                      </button>
                      <button onClick={() => setFitWindow((f) => !f)}>{fitWindow ? "Fit: Window" : "Fit: 1:1"}</button>
                    </div>
                    <input type="range" className="scrub" disabled title={PENDING.experiment} aria-label="Preview buffer scrub (not bridged)" />

                    <div className="subtabs" style={{ marginTop: 8 }} role="tablist" aria-label="Configuration">
                      <button className={configTab === "app" ? "active" : ""} onClick={() => setConfigTab("app")}>
                        App config (config.json)
                      </button>
                      <button className={configTab === "script" ? "active" : ""} onClick={() => setConfigTab("script")}>
                        Camera script
                      </button>
                    </div>
                    <div className="subtab-body">
                      {configTab === "app" && (
                        <>
                          <div className="toolbar">
                            <button disabled title={PENDING.config}>Reset</button>
                            <button disabled title={PENDING.config}>Save</button>
                            <button disabled title={PENDING.config}>Browse…</button>
                            <label>
                              Profile:{" "}
                              <select disabled title={PENDING.config}>
                                <option>&lt;no prof&gt;</option>
                              </select>
                            </label>
                            <button disabled title={PENDING.config}>Save Profile</button>
                            <button disabled title={PENDING.config}>Show Diff</button>
                          </div>
                          <div className="config-grid">
                            <div className="config-group">
                              <h5>realtime_processing (bridged — schema v3)</h5>
                              <div className="row">
                                <label>
                                  <input
                                    type="checkbox"
                                    checked={procEnabled}
                                    onChange={(e) => setProcEnabled(e.target.checked)}
                                  />{" "}
                                  realtime processing
                                </label>
                              </div>
                              <div className="row">
                                <label>
                                  px→µm{" "}
                                  <input
                                    type="text"
                                    style={{ width: 70 }}
                                    value={pixelToMicron}
                                    onChange={(e) => setPixelToMicron(e.target.value)}
                                  />
                                </label>
                                <button className="btn" onClick={onApplyProcessing} disabled={!ready} title={ready ? undefined : "Backend is not initialized"}>
                                  Apply
                                </button>
                              </div>
                              {stats?.valid && (
                                <p className="mono">
                                  algo {stats.algo_fps1s.toFixed(1)} · valid {stats.valid_fps1s.toFixed(1)} · invalid{" "}
                                  {stats.invalid_fps1s.toFixed(1)} fps · px→µm {stats.pixel_to_micron}
                                </p>
                              )}
                            </div>
                            <div className="config-group" title={PENDING.config}>
                              <h5>General / image_processing</h5>
                              <p className="pending-note">
                                Full config round-trip (thresholds, background, batching, flush interval, profiles) is not bridged yet —
                                BE-3 (#273).
                              </p>
                            </div>
                          </div>
                        </>
                      )}
                      {configTab === "script" && (
                        <>
                          <div className="toolbar">
                            <button disabled title={PENDING.script}>Reset</button>
                            <button disabled title={PENDING.script}>Save</button>
                            <button disabled title={PENDING.script}>Apply to Camera</button>
                          </div>
                          <textarea
                            className="script-editor"
                            disabled
                            title={PENDING.script}
                            value={"// Camera script editing is not bridged yet — BE-2 (#272)."}
                            readOnly
                          />
                        </>
                      )}
                    </div>
                  </>
                )}

                {expTab === "monitoring" && (
                  <>
                    <div className="toolbar">
                      <button disabled title={PENDING.monitoring}>Clear Buffer</button>
                      <button disabled title={PENDING.monitoring}>Sort Trigger</button>
                      <button disabled title={PENDING.monitoring}>Periodic Test</button>
                    </div>
                    <div className="config-grid" style={{ flex: 1 }}>
                      <div className="config-group" title={PENDING.monitoring}>
                        <h5>Deformability vs Area (µm²)</h5>
                        <p className="pending-note">Monitoring scatter/isoelastic charts are not bridged yet — BE-5 (#275).</p>
                      </div>
                      <div className="config-group" title={PENDING.monitoring}>
                        <h5>Ring Width Distribution</h5>
                        <p className="pending-note">Histogram data is not bridged yet — BE-5 (#275).</p>
                      </div>
                      <div className="config-group" title={PENDING.monitoring}>
                        <h5>Tune Params</h5>
                        <p className="pending-note">
                          Filter thresholds, target group, and multi-image settings are not bridged yet — BE-5 (#275).
                        </p>
                      </div>
                    </div>
                  </>
                )}
              </>
            )}

            {/* ---- Review ---- */}
            {tab === "review" && (
              <>
                <div className="toolbar">
                  <button onClick={onSelectHdf} disabled={!ready} title={ready ? undefined : "Backend is not initialized"}>
                    Select HDF File…
                  </button>
                  <button disabled title={PENDING.review}>Close File</button>
                  <button disabled title={PENDING.review}>Export Metrics to CSV…</button>
                  <button disabled title={PENDING.review}>Export All…</button>
                  <button disabled title={PENDING.review}>Batch Metrics…</button>
                  <button disabled title={PENDING.review}>Regenerate masks…</button>
                  <span className="legend">
                    <span className="chip"><span className="swatch" style={{ background: "#2b6cb0" }} /> Target</span>
                    <span className="chip"><span className="swatch" style={{ background: "#1a7f37" }} /> Valid</span>
                    <span className="chip"><span className="swatch" style={{ background: "#b42318" }} /> Invalid</span>
                  </span>
                  <span className="path-label right">{reviewing ? reviewPath : "No file selected"}</span>
                </div>
                <div className="subtabs" role="tablist" aria-label="Review views">
                  <button className={reviewTab === "raw" ? "active" : ""} onClick={() => setReviewTab("raw")}>
                    Raw Frames
                  </button>
                  <button disabled title={PENDING.review}>Valid Frames</button>
                  <button disabled title={PENDING.review}>Invalid Frames</button>
                  <button disabled title={PENDING.review}>Charts</button>
                </div>
                <div className="subtab-body">
                  <div className="review-split">
                    <div className="frames">
                      <div className="canvas-wrap">
                        {!reviewing && <span className="canvas-hint">No recording loaded — Select HDF File…</span>}
                        <canvas ref={reviewCanvasRef} className={fitWindow ? "fit" : ""} />
                      </div>
                      {reviewing && range.count > 0 && (
                        <>
                          <input
                            type="range"
                            className="scrub"
                            min={range.earliest}
                            max={range.latest}
                            value={reviewIndex}
                            onChange={(e) => onScrub(Number(e.target.value))}
                            aria-label="Frame scrubber"
                          />
                          <span className="mono">
                            frame {reviewIndex} of [{range.earliest}…{range.latest}] ({range.count} available)
                          </span>
                        </>
                      )}
                    </div>
                    <div className="table-panel" title={PENDING.review}>
                      <table className="metrics-table">
                        <thead>
                          <tr>
                            <th>Index</th>
                            <th>Timestamp</th>
                            <th>Object Id</th>
                            <th>Object Count</th>
                            <th>Track Id</th>
                            <th>Deformability</th>
                          </tr>
                        </thead>
                        <tbody>
                          <tr>
                            <td colSpan={6} style={{ color: "#777" }}>
                              Frame/object metrics are not bridged yet — BE-6 (#276).
                            </td>
                          </tr>
                        </tbody>
                      </table>
                    </div>
                  </div>
                </div>
              </>
            )}
          </div>
        </main>
      </div>

      {/* ---- Status bar ---- */}
      <footer className="statusbar">
        <span>
          Bridge ABI: {abi ?? "…"} · backend: {ready ? "ready" : "not initialized"}
          {reviewing ? " · reviewing" : ""}
          {recording ? " · recording" : ""}
        </span>
        <button className="log-toggle" onClick={() => setShowLog((s) => !s)}>
          Log {showLog ? "▾" : "▸"} ({log.length})
        </button>
        <span className="metrics">
          Display={displayFps.toFixed(1)} fps | Algo={algoFps.toFixed(1)}/s | Valid={validFps.toFixed(1)}/s | Invalid=
          {invalidFps.toFixed(1)}/s | Camera={running ? "running" : camStatus}, {dataRate.toFixed(1)} MB/s | Experiment: not
          migrated
        </span>
      </footer>
      {showLog && (
        <div className="log-drawer" role="log">
          {log.length === 0 ? <div>no log entries</div> : log.map((l, i) => <div key={i}>{l}</div>)}
        </div>
      )}

      {/* ---- Configure Mock modal ---- */}
      {showMockConfig && (
        <div className="modal-backdrop" onClick={() => setShowMockConfig(false)}>
          <div className="modal" onClick={(e) => e.stopPropagation()} role="dialog" aria-label="Configure mock camera">
            <h3>Configure Mock Camera</h3>
            <div className="row">
              <input
                type="text"
                placeholder="mock frame directory (folder of .tiff/.png)"
                value={frameDir}
                onChange={(e) => setFrameDir(e.target.value)}
              />
              <button
                className="btn"
                onClick={async () => {
                  const picked = await open({ directory: true, title: "Select mock frame directory" });
                  if (typeof picked === "string") setFrameDir(picked);
                }}
              >
                Browse…
              </button>
            </div>
            <div className="row">
              <label>
                Frame interval{" "}
                <input
                  type="number"
                  style={{ width: 80, flex: "none" }}
                  value={intervalMs}
                  onChange={(e) => setIntervalMs(e.target.value)}
                />{" "}
                ms
              </label>
              <label>
                <input type="checkbox" checked={loopFiles} onChange={(e) => setLoopFiles(e.target.checked)} /> loop files
              </label>
            </div>
            <div className="actions">
              <button className="btn" onClick={() => setShowMockConfig(false)}>Cancel</button>
              <button className="btn" onClick={onConfigureMock} disabled={!frameDir} title={frameDir ? undefined : "Pick a frame directory first"}>
                Apply
              </button>
            </div>
          </div>
        </div>
      )}

      {/* ---- About modal ---- */}
      {showAbout && (
        <div className="modal-backdrop" onClick={() => setShowAbout(false)}>
          <div className="modal" onClick={(e) => e.stopPropagation()} role="dialog" aria-label="About">
            <h3>MIB Studio (React + Tauri)</h3>
            <p>
              Desktop shell for the Qt-free MIB backend (epic #246).
              <br />
              Bridge ABI version: {abi ?? "unknown"} · backend {ready ? "initialized" : "not initialized"}.
            </p>
            <div className="actions">
              <button className="btn" onClick={() => setShowAbout(false)}>Close</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
