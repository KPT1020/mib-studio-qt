import { useState } from "react";
import { useCaptureStore } from "../../stores/captureStore";
import { useAppStore } from "../../stores/appStore";
import {
  discoverCameras,
  discoverFramegrabbers,
  connectCamera,
} from "../../hooks/useBackend";

type DeviceTab = "cameras" | "framegrabbers";

export function ConnectTab() {
  const [deviceTab, setDeviceTab] = useState<DeviceTab>("cameras");
  const [status, setStatus] = useState("Select a device and click Connect.");

  const cameras = useCaptureStore((s) => s.cameras);
  const framegrabbers = useCaptureStore((s) => s.framegrabbers);
  const selectedCameraIndex = useCaptureStore((s) => s.selectedCameraIndex);
  const selectedFramegrabberIndex = useCaptureStore((s) => s.selectedFramegrabberIndex);
  const setCameras = useCaptureStore((s) => s.setCameras);
  const setFramegrabbers = useCaptureStore((s) => s.setFramegrabbers);
  const setSelectedCamera = useCaptureStore((s) => s.setSelectedCamera);
  const setSelectedFramegrabber = useCaptureStore((s) => s.setSelectedFramegrabber);
  const setCameraConfigured = useCaptureStore((s) => s.setCameraConfigured);
  const openDialog = useAppStore((s) => s.openDialog);

  const handleRefresh = async () => {
    setStatus("Discovering devices...");
    try {
      const [cams, fgs] = await Promise.all([
        discoverCameras(),
        discoverFramegrabbers(),
      ]);
      setCameras(cams);
      setFramegrabbers(fgs);
      setStatus(
        `Found ${cams.length} camera(s) and ${fgs.length} framegrabber(s).`
      );
    } catch (e) {
      setStatus(`Discovery failed: ${e}`);
    }
  };

  const handleConnect = async () => {
    if (deviceTab === "cameras" && selectedCameraIndex !== null) {
      const cam = cameras[selectedCameraIndex];
      setStatus(`Connecting to ${cam.label}...`);
      try {
        await connectCamera(cam.interfaceIndex, cam.deviceIndex, cam.label);
        setCameraConfigured(true);
        setStatus(`Connected to ${cam.label}`);
      } catch (e) {
        setStatus(`Connection failed: ${e}`);
      }
    } else if (deviceTab === "framegrabbers" && selectedFramegrabberIndex !== null) {
      const fg = framegrabbers[selectedFramegrabberIndex];
      setStatus(`Connecting to ${fg.label}...`);
      try {
        await connectCamera(fg.interfaceIndex, fg.deviceIndex, fg.label);
        setCameraConfigured(true);
        setStatus(`Connected to ${fg.label}`);
      } catch (e) {
        setStatus(`Connection failed: ${e}`);
      }
    }
  };

  const items = deviceTab === "cameras" ? cameras : framegrabbers;
  const selectedIndex = deviceTab === "cameras" ? selectedCameraIndex : selectedFramegrabberIndex;
  const onSelect = deviceTab === "cameras" ? setSelectedCamera : setSelectedFramegrabber;

  return (
    /* QVBoxLayout (default margins ~9px) */
    <div style={{ display: "flex", flexDirection: "column", height: "100%", padding: 9, gap: 0 }}>
      {/* QLabel "Available devices:" */}
      <label style={{ fontSize: 12, marginBottom: 4 }}>Available devices:</label>

      {/* QTabWidget with 2 tabs: "Cameras" and "Framegrabbers" */}
      <div style={{ flex: 1, display: "flex", flexDirection: "column", minHeight: 0 }}>
        {/* Tab bar */}
        <div className="qt-tab-bar">
          <button
            className="qt-tab"
            data-active={deviceTab === "cameras"}
            onClick={() => setDeviceTab("cameras")}
          >
            Cameras
          </button>
          <button
            className="qt-tab"
            data-active={deviceTab === "framegrabbers"}
            onClick={() => setDeviceTab("framegrabbers")}
          >
            Framegrabbers
          </button>
        </div>

        {/* QListWidget (single selection mode) filling the tab */}
        <ul
          className="qt-list"
          style={{
            flex: 1,
            overflow: "auto",
            border: "1px solid #c0c0c0",
            borderTop: "none",
          }}
        >
          {items.map((item, i) => (
            <li
              key={`${item.interfaceIndex}-${item.deviceIndex}`}
              className="qt-list-item"
              data-selected={selectedIndex === i}
              onClick={() => onSelect(i)}
            >
              {item.label}
            </li>
          ))}
          {items.length === 0 && (
            <li className="qt-list-item" style={{ color: "#999", textAlign: "center" }}>
              {deviceTab === "cameras"
                ? "No cameras found. Click Refresh."
                : "No framegrabbers found. Click Refresh."}
            </li>
          )}
        </ul>
      </div>

      {/* Button row (QHBoxLayout): Refresh, Connect, spacer, "Configure Mock..." */}
      <div style={{ display: "flex", alignItems: "center", gap: 6, marginTop: 6 }}>
        <button className="qt-btn" onClick={handleRefresh}>Refresh</button>
        <button className="qt-btn" onClick={handleConnect}>Connect</button>
        <div style={{ flex: 1 }} />
        <button className="qt-btn" onClick={() => openDialog("mockConfig")}>Configure Mock...</button>
      </div>

      {/* QLabel status at bottom */}
      <label style={{ fontSize: 12, color: "#666", marginTop: 4 }}>{status}</label>
    </div>
  );
}
