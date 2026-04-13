import { useState } from "react";
import {
  connectAutofocus,
  disconnectAutofocus,
  setAutofocusEnabled,
  increaseVoltage,
  decreaseVoltage,
} from "../../hooks/useBackend";
import { Button } from "../ui/button";
import { Input } from "../ui/input";
import { Checkbox } from "../ui/checkbox";
import { Label } from "../ui/label";
import { Separator } from "../ui/separator";
import { Card, CardContent, CardHeader, CardTitle } from "../ui/card";
import { Minus, Plus } from "lucide-react";

export function NanopositionerPanel() {
  const [comPort, setComPort] = useState("COM1");
  const [baudRate, setBaudRate] = useState("115200");
  const [deviceAddress, setDeviceAddress] = useState(1);
  const [connected, setConnected] = useState(false);
  const [enabled, setEnabled] = useState(false);
  const [targetRingWidth, setTargetRingWidth] = useState(20.0);
  const [voltage, setVoltage] = useState("-- V");
  const [status, setStatus] = useState("Not connected");
  const [voltageStep, setVoltageStep] = useState(1);

  const handleConnect = async () => {
    try {
      const port = parseInt(comPort.replace("COM", ""));
      await connectAutofocus(port, parseInt(baudRate), deviceAddress);
      setConnected(true);
      setStatus("Connected");
    } catch (e) {
      setStatus(`Error: ${e}`);
    }
  };

  const handleDisconnect = async () => {
    try {
      await disconnectAutofocus();
      setConnected(false);
      setStatus("Not connected");
      setVoltage("-- V");
    } catch (e) {
      setStatus(`Error: ${e}`);
    }
  };

  const handleToggleAutofocus = async (on: boolean) => {
    setEnabled(on);
    try {
      await setAutofocusEnabled(on);
    } catch {
      setEnabled(!on);
    }
  };

  return (
    <div className="flex flex-col p-3 gap-2">
      <Card>
        <CardHeader className="p-3 pb-0">
          <CardTitle>Nanopositioner Autofocus</CardTitle>
        </CardHeader>
        <CardContent className="p-3 flex flex-col gap-3">
          {/* Connection settings */}
          <div className="form-grid">
            <Label>COM Port:</Label>
            <div className="flex gap-1">
              <select
                className="select-styled flex-1"
                value={comPort}
                onChange={(e) => setComPort(e.target.value)}
              >
                {Array.from({ length: 20 }, (_, i) => (
                  <option key={i} value={`COM${i + 1}`}>COM{i + 1}</option>
                ))}
              </select>
              <Button size="sm" variant="outline">Refresh</Button>
            </div>

            <Label>Baud Rate:</Label>
            <select
              className="select-styled"
              value={baudRate}
              onChange={(e) => setBaudRate(e.target.value)}
            >
              {["9600", "19200", "38400", "57600", "115200"].map((r) => (
                <option key={r} value={r}>{r}</option>
              ))}
            </select>

            <Label>Device Address:</Label>
            <Input
              type="number"
              min={0}
              max={255}
              value={deviceAddress}
              onChange={(e) => setDeviceAddress(Number(e.target.value))}
            />
          </div>

          {/* Connect/Disconnect buttons */}
          <div className="flex gap-1">
            <Button size="sm" onClick={handleConnect} disabled={connected}>Connect</Button>
            <Button size="sm" variant="outline" onClick={handleDisconnect} disabled={!connected}>
              Disconnect
            </Button>
          </div>

          {/* Autofocus toggle */}
          <div className="flex items-center gap-2">
            <Checkbox
              id="autofocus-enable"
              checked={enabled}
              onCheckedChange={(v) => handleToggleAutofocus(v === true)}
              disabled={!connected}
            />
            <Label htmlFor="autofocus-enable" className="text-xs">Enable Autofocus</Label>
          </div>

          {/* Target ring width */}
          <div className="form-grid">
            <Label>Target ring width:</Label>
            <Input
              type="number"
              step={0.01}
              min={1}
              max={100}
              value={targetRingWidth}
              onChange={(e) => setTargetRingWidth(Number(e.target.value))}
            />
          </div>

          {/* Status display */}
          <div className="form-grid">
            <Label>Status:</Label>
            <span className="text-xs">{status}</span>
            <Label>Voltage:</Label>
            <span className="text-xs">{voltage}</span>
          </div>

          <Separator />

          {/* Manual Control */}
          <span className="font-semibold text-xs">Manual Control:</span>
          <div className="flex items-center gap-1">
            <Button
              size="icon"
              variant="outline"
              onClick={() => decreaseVoltage()}
              disabled={!connected}
            >
              <Minus className="h-3.5 w-3.5" />
            </Button>
            <Button
              size="icon"
              variant="outline"
              onClick={() => increaseVoltage()}
              disabled={!connected}
            >
              <Plus className="h-3.5 w-3.5" />
            </Button>
            <div className="flex-1" />
            <span className="text-xs text-muted-foreground">Step:</span>
            <Input
              type="number"
              min={1}
              max={100}
              value={voltageStep}
              onChange={(e) => setVoltageStep(Number(e.target.value))}
              disabled={!connected}
              className="w-14"
            />
            <span className="text-xs text-muted-foreground">V</span>
          </div>
        </CardContent>
      </Card>
    </div>
  );
}
