import { useState } from "react";
import {
  connectAutofocus,
  disconnectAutofocus,
  setAutofocusEnabled,
  increaseVoltage,
  decreaseVoltage,
} from "../../hooks/useBackend";

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
    <div className="p-1.5" style={{ padding: "var(--spacing-sm)" }}>
      <fieldset className="border border-neutral-300 rounded p-2">
        <legend className="text-xs font-semibold px-1">Nanopositioner Autofocus</legend>

        {/* Connection form */}
        <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-1 text-xs mb-2">
          <label className="text-right">COM Port:</label>
          <div className="flex gap-1">
            <select
              value={comPort}
              onChange={(e) => setComPort(e.target.value)}
              className="flex-1 border border-neutral-400 rounded px-1 py-0.5"
            >
              {Array.from({ length: 20 }, (_, i) => (
                <option key={i} value={`COM${i + 1}`}>
                  COM{i + 1}
                </option>
              ))}
            </select>
            <button className="px-2 py-0.5 bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">
              Refresh
            </button>
          </div>

          <label className="text-right">Baud Rate:</label>
          <select
            value={baudRate}
            onChange={(e) => setBaudRate(e.target.value)}
            className="border border-neutral-400 rounded px-1 py-0.5"
          >
            {["9600", "19200", "38400", "57600", "115200"].map((r) => (
              <option key={r} value={r}>{r}</option>
            ))}
          </select>

          <label className="text-right">Device Address:</label>
          <input
            type="number"
            min={0}
            max={255}
            value={deviceAddress}
            onChange={(e) => setDeviceAddress(Number(e.target.value))}
            className="border border-neutral-400 rounded px-1 py-0.5"
          />
        </div>

        {/* Connect/Disconnect */}
        <div className="flex gap-1 mb-2">
          <button
            onClick={handleConnect}
            disabled={connected}
            className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
          >
            Connect
          </button>
          <button
            onClick={handleDisconnect}
            disabled={!connected}
            className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
          >
            Disconnect
          </button>
          <div className="flex-1" />
        </div>

        {/* Autofocus toggle */}
        <label className="flex items-center gap-1 text-xs mb-2">
          <input
            type="checkbox"
            checked={enabled}
            onChange={(e) => handleToggleAutofocus(e.target.checked)}
            disabled={!connected}
          />
          Enable Autofocus
        </label>

        {/* Target ring width */}
        <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-1 text-xs mb-2">
          <label className="text-right" title="Ring width setpoint for autofocus">
            Target ring width:
          </label>
          <input
            type="number"
            step={0.01}
            min={1}
            max={100}
            value={targetRingWidth}
            onChange={(e) => setTargetRingWidth(Number(e.target.value))}
            className="border border-neutral-400 rounded px-1 py-0.5"
          />
        </div>

        {/* Status */}
        <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-0.5 text-xs mb-2">
          <span className="text-right text-neutral-600">Status:</span>
          <span>{status}</span>
          <span className="text-right text-neutral-600">Voltage:</span>
          <span>{voltage}</span>
        </div>

        <hr className="border-neutral-300 my-2" />

        {/* Manual control */}
        <p className="text-xs font-bold mb-1">Manual Control:</p>
        <div className="flex items-center gap-1">
          <button
            onClick={() => decreaseVoltage()}
            disabled={!connected}
            className="px-3 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
          >
            -
          </button>
          <button
            onClick={() => increaseVoltage()}
            disabled={!connected}
            className="px-3 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
          >
            +
          </button>
          <div className="flex-1" />
          <span className="text-xs">Step:</span>
          <div className="flex items-center">
            <input
              type="number"
              min={1}
              max={100}
              value={voltageStep}
              onChange={(e) => setVoltageStep(Number(e.target.value))}
              disabled={!connected}
              className="w-14 border border-neutral-400 rounded px-1 py-0.5 text-xs"
            />
            <span className="text-xs ml-0.5">V</span>
          </div>
        </div>
      </fieldset>
    </div>
  );
}
