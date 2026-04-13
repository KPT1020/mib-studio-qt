import { useState } from "react";
import { useCaptureStore } from "../../stores/captureStore";
import { useAppStore } from "../../stores/appStore";
import {
  discoverCameras,
  discoverFramegrabbers,
  connectCamera,
} from "../../hooks/useBackend";
import { Tabs, TabsList, TabsTrigger, TabsContent } from "../ui/tabs";
import { Button } from "../ui/button";
import { cn } from "@/lib/utils";

export function ConnectTab() {
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
  const [deviceTab, setDeviceTab] = useState("cameras");

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
    <div className="flex flex-col h-full p-3 gap-2">
      <span className="text-sm text-muted-foreground">Available devices:</span>

      <Tabs value={deviceTab} onValueChange={setDeviceTab} className="flex-1 flex flex-col min-h-0">
        <TabsList>
          <TabsTrigger value="cameras">Cameras</TabsTrigger>
          <TabsTrigger value="framegrabbers">Framegrabbers</TabsTrigger>
        </TabsList>

        <TabsContent value="cameras" className="flex-1 min-h-0">
          <DeviceList items={cameras} selectedIndex={selectedCameraIndex} onSelect={setSelectedCamera} emptyText="No cameras found. Click Refresh." />
        </TabsContent>
        <TabsContent value="framegrabbers" className="flex-1 min-h-0">
          <DeviceList items={framegrabbers} selectedIndex={selectedFramegrabberIndex} onSelect={setSelectedFramegrabber} emptyText="No framegrabbers found. Click Refresh." />
        </TabsContent>
      </Tabs>

      <div className="flex items-center gap-2">
        <Button size="sm" onClick={handleRefresh}>Refresh</Button>
        <Button size="sm" onClick={handleConnect}>Connect</Button>
        <div className="flex-1" />
        <Button size="sm" variant="outline" onClick={() => openDialog("mockConfig")}>Configure Mock...</Button>
      </div>

      <span className="text-xs text-muted-foreground">{status}</span>
    </div>
  );
}

function DeviceList({ items, selectedIndex, onSelect, emptyText }: {
  items: { interfaceIndex: number; deviceIndex: number; label: string }[];
  selectedIndex: number | null;
  onSelect: (index: number) => void;
  emptyText: string;
}) {
  return (
    <div className="h-full overflow-auto rounded-md border border-border bg-background">
      {items.map((item, i) => (
        <div
          key={`${item.interfaceIndex}-${item.deviceIndex}`}
          className={cn(
            "px-3 py-1.5 text-sm cursor-pointer border-b border-border/50 transition-colors",
            selectedIndex === i
              ? "bg-primary text-primary-foreground"
              : "hover:bg-muted"
          )}
          onClick={() => onSelect(i)}
        >
          {item.label}
        </div>
      ))}
      {items.length === 0 && (
        <div className="px-3 py-4 text-sm text-center text-muted-foreground">
          {emptyText}
        </div>
      )}
    </div>
  );
}
