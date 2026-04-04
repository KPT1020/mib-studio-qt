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
    <div
      style={{
        display: "flex",
        flexDirection: "column",
        padding: 6,
        gap: 8,
      }}
    >
      {/* QGroupBox "Nanopositioner Autofocus" */}
      <fieldset className="qt-groupbox">
        <legend>Nanopositioner Autofocus</legend>

        {/* QFormLayout (label right-aligned) */}
        <div className="qt-form">
          {/* COM Port: QComboBox + Refresh button (QHBoxLayout) */}
          <label>COM Port:</label>
          <div style={{ display: "flex", gap: 4 }}>
            <select
              className="qt-select"
              value={comPort}
              onChange={(e) => setComPort(e.target.value)}
              style={{ flex: 1 }}
            >
              {Array.from({ length: 20 }, (_, i) => (
                <option key={i} value={`COM${i + 1}`}>
                  COM{i + 1}
                </option>
              ))}
            </select>
            <button className="qt-btn">Refresh</button>
          </div>

          {/* Baud Rate: QComboBox */}
          <label>Baud Rate:</label>
          <select
            className="qt-select"
            value={baudRate}
            onChange={(e) => setBaudRate(e.target.value)}
          >
            {["9600", "19200", "38400", "57600", "115200"].map((r) => (
              <option key={r} value={r}>
                {r}
              </option>
            ))}
          </select>

          {/* Device Address: QSpinBox (0-255, default 1) */}
          <label>Device Address:</label>
          <input
            className="qt-input"
            type="number"
            min={0}
            max={255}
            value={deviceAddress}
            onChange={(e) => setDeviceAddress(Number(e.target.value))}
          />
        </div>

        {/* Connect/Disconnect buttons + spacer */}
        <div
          style={{
            display: "flex",
            gap: 4,
            marginTop: 6,
          }}
        >
          <button
            className="qt-btn"
            onClick={handleConnect}
            disabled={connected}
          >
            Connect
          </button>
          <button
            className="qt-btn"
            onClick={handleDisconnect}
            disabled={!connected}
          >
            Disconnect
          </button>
          <div style={{ flex: 1 }} />
        </div>

        {/* "Enable Autofocus" checkbox (disabled until connected) */}
        <div style={{ marginTop: 6 }}>
          <label className="qt-checkbox">
            <input
              type="checkbox"
              checked={enabled}
              onChange={(e) => handleToggleAutofocus(e.target.checked)}
              disabled={!connected}
            />
            Enable Autofocus
          </label>
        </div>

        {/* Target ring width: QDoubleSpinBox */}
        <div className="qt-form" style={{ marginTop: 6 }}>
          <label>Target ring width:</label>
          <input
            className="qt-input"
            type="number"
            step={0.01}
            min={1}
            max={100}
            value={targetRingWidth}
            onChange={(e) => setTargetRingWidth(Number(e.target.value))}
          />
        </div>

        {/* Status form: Status + Voltage labels */}
        <div className="qt-form" style={{ marginTop: 6 }}>
          <label>Status:</label>
          <span style={{ fontSize: 12 }}>{status}</span>
          <label>Voltage:</label>
          <span style={{ fontSize: 12 }}>{voltage}</span>
        </div>

        {/* QFrame::HLine separator */}
        <hr className="qt-separator" style={{ margin: "8px 0" }} />

        {/* "Manual Control:" label (bold) */}
        <span style={{ fontWeight: "bold", fontSize: 12 }}>Manual Control:</span>

        {/* Decrease (-) / Increase (+) buttons + spacer + "Step:" label + QSpinBox (1-100 V) */}
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 4,
            marginTop: 4,
          }}
        >
          <button
            className="qt-btn"
            onClick={() => decreaseVoltage()}
            disabled={!connected}
          >
            -
          </button>
          <button
            className="qt-btn"
            onClick={() => increaseVoltage()}
            disabled={!connected}
          >
            +
          </button>
          <div style={{ flex: 1 }} />
          <span style={{ fontSize: 12 }}>Step:</span>
          <input
            className="qt-input"
            type="number"
            min={1}
            max={100}
            value={voltageStep}
            onChange={(e) => setVoltageStep(Number(e.target.value))}
            disabled={!connected}
            style={{ width: 56 }}
          />
          <span style={{ fontSize: 12 }}>V</span>
        </div>
      </fieldset>

      {/* Vertical spacer */}
      <div style={{ flex: 1 }} />
    </div>
  );
}
