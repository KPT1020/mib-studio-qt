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

  return (
    <div className="flex flex-col h-full p-0">
      {/* Available devices label */}
      <div className="px-3 py-2 text-sm">Available devices:</div>

      {/* Device tabs */}
      <div className="flex-1 flex flex-col mx-0">
        {/* Tab headers */}
        <div className="flex border-b border-neutral-300">
          <button
            onClick={() => setDeviceTab("cameras")}
            className={`px-4 py-1.5 text-xs border-r border-neutral-300 ${
              deviceTab === "cameras"
                ? "bg-white font-semibold border-b-2 border-b-blue-500"
                : "bg-neutral-100 hover:bg-neutral-200 cursor-pointer"
            }`}
          >
            Cameras
          </button>
          <button
            onClick={() => setDeviceTab("framegrabbers")}
            className={`px-4 py-1.5 text-xs border-r border-neutral-300 ${
              deviceTab === "framegrabbers"
                ? "bg-white font-semibold border-b-2 border-b-blue-500"
                : "bg-neutral-100 hover:bg-neutral-200 cursor-pointer"
            }`}
          >
            Framegrabbers
          </button>
        </div>

        {/* Device list */}
        <div className="flex-1 overflow-y-auto bg-white border border-neutral-300 mx-3">
          {deviceTab === "cameras" && (
            <ul className="list-none m-0 p-0">
              {cameras.map((cam, i) => (
                <li
                  key={`${cam.interfaceIndex}-${cam.deviceIndex}`}
                  onClick={() => setSelectedCamera(i)}
                  className={`px-3 py-1.5 text-sm cursor-pointer ${
                    selectedCameraIndex === i
                      ? "bg-blue-100 font-medium"
                      : "hover:bg-neutral-50"
                  }`}
                >
                  {cam.label}
                </li>
              ))}
              {cameras.length === 0 && (
                <li className="px-3 py-4 text-sm text-neutral-400 text-center">
                  No cameras found. Click Refresh.
                </li>
              )}
            </ul>
          )}
          {deviceTab === "framegrabbers" && (
            <ul className="list-none m-0 p-0">
              {framegrabbers.map((fg, i) => (
                <li
                  key={`${fg.interfaceIndex}-${fg.deviceIndex}`}
                  onClick={() => setSelectedFramegrabber(i)}
                  className={`px-3 py-1.5 text-sm cursor-pointer ${
                    selectedFramegrabberIndex === i
                      ? "bg-blue-100 font-medium"
                      : "hover:bg-neutral-50"
                  }`}
                >
                  {fg.label}
                </li>
              ))}
              {framegrabbers.length === 0 && (
                <li className="px-3 py-4 text-sm text-neutral-400 text-center">
                  No framegrabbers found. Click Refresh.
                </li>
              )}
            </ul>
          )}
        </div>
      </div>

      {/* Button row */}
      <div className="flex items-center gap-2 px-3 py-2">
        <button
          onClick={handleRefresh}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          Refresh
        </button>
        <button
          onClick={handleConnect}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          Connect
        </button>
        <div className="flex-1" />
        <button
          onClick={() => openDialog("mockConfig")}
          className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded"
        >
          Configure Mock...
        </button>
      </div>

      {/* Status */}
      <div className="px-3 py-1 text-xs text-neutral-600 border-t border-neutral-200">
        {status}
      </div>
    </div>
  );
}
