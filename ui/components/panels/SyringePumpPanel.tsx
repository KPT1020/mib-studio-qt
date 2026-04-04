import { useState } from "react";
import type { PumpId, PumpDirection } from "../../types/backend";
import {
  connectPump,
  disconnectPump,
  setPumpFlowRate,
  setPumpDirection,
  startPump,
  stopPump,
  purgePump,
} from "../../hooks/useBackend";

interface PumpState {
  connected: boolean;
  flowRate: number;
  flowUnit: string;
  direction: PumpDirection;
  status: string;
  flowStatus: string;
  volume: string;
}

const defaultPumpState: PumpState = {
  connected: false,
  flowRate: 0,
  flowUnit: "uL/min",
  direction: "infuse",
  status: "Not connected",
  flowStatus: "--",
  volume: "--",
};

function PumpGroup({ pumpId, title }: { pumpId: PumpId; title: string }) {
  const [state, setState] = useState<PumpState>(defaultPumpState);

  const handleConnect = async () => {
    try {
      await connectPump(pumpId, 1, 9600, 1);
      setState((s) => ({ ...s, connected: true, status: "Connected" }));
    } catch (e) {
      setState((s) => ({ ...s, status: `Error: ${e}` }));
    }
  };

  const handleDisconnect = async () => {
    try {
      await disconnectPump(pumpId);
      setState(defaultPumpState);
    } catch {
      // Ignore
    }
  };

  return (
    <fieldset className="qt-groupbox">
      <legend>{title}</legend>

      <div
        style={{
          display: "flex",
          flexDirection: "column",
          gap: 6,
          padding: 6,
        }}
      >
        {/* Connect/Disconnect + spacer */}
        <div style={{ display: "flex", gap: 4 }}>
          <button
            className="qt-btn"
            onClick={handleConnect}
            disabled={state.connected}
          >
            Connect
          </button>
          <button
            className="qt-btn"
            onClick={handleDisconnect}
            disabled={!state.connected}
          >
            Disconnect
          </button>
          <div style={{ flex: 1 }} />
        </div>

        {/* QFormLayout: Flow Rate (QDoubleSpinBox 4 decimals + unit combo), Direction */}
        <div className="qt-form">
          <label>Flow Rate:</label>
          <div style={{ display: "flex", gap: 4 }}>
            <input
              className="qt-input"
              type="number"
              step={0.0001}
              min={0}
              max={100000}
              value={state.flowRate}
              onChange={(e) => {
                const val = Number(e.target.value);
                setState((s) => ({ ...s, flowRate: val }));
                setPumpFlowRate(pumpId, val, state.flowUnit === "uL/min" ? 0 : 1);
              }}
              disabled={!state.connected}
              style={{ flex: 1 }}
            />
            <select
              className="qt-select"
              value={state.flowUnit}
              onChange={(e) => setState((s) => ({ ...s, flowUnit: e.target.value }))}
              disabled={!state.connected}
            >
              <option value="uL/min">uL/min</option>
              <option value="mL/min">mL/min</option>
            </select>
          </div>

          <label>Direction:</label>
          <select
            className="qt-select"
            value={state.direction}
            onChange={(e) => {
              const dir = e.target.value as PumpDirection;
              setState((s) => ({ ...s, direction: dir }));
              setPumpDirection(pumpId, dir);
            }}
            disabled={!state.connected}
          >
            <option value="infuse">Infuse</option>
            <option value="withdraw">Withdraw</option>
          </select>
        </div>

        {/* Start/Stop/Purge + spacer */}
        <div style={{ display: "flex", gap: 4 }}>
          <button
            className="qt-btn"
            onClick={() => startPump(pumpId)}
            disabled={!state.connected}
          >
            Start
          </button>
          <button
            className="qt-btn"
            onClick={() => stopPump(pumpId)}
            disabled={!state.connected}
          >
            Stop
          </button>
          <button
            className="qt-btn"
            onClick={() => purgePump(pumpId, state.direction)}
            disabled={!state.connected}
          >
            Purge
          </button>
          <div style={{ flex: 1 }} />
        </div>

        {/* Status form: Status, Flow, Volume labels */}
        <div className="qt-form">
          <label>Status:</label>
          <span style={{ fontSize: 12 }}>{state.status}</span>
          <label>Flow:</label>
          <span style={{ fontSize: 12 }}>{state.flowStatus}</span>
          <label>Volume:</label>
          <span style={{ fontSize: 12 }}>{state.volume}</span>
        </div>
      </div>
    </fieldset>
  );
}

export function SyringePumpPanel() {
  return (
    <div
      style={{
        display: "flex",
        flexDirection: "column",
        padding: 6,
        gap: 4,
      }}
    >
      <PumpGroup pumpId="sample" title="Sample Pump" />
      <PumpGroup pumpId="sheath" title="Sheath Pump" />

      {/* Bottom spacer */}
      <div style={{ flex: 1 }} />
    </div>
  );
}
