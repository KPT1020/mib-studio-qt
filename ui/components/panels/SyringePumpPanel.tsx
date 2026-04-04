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
    <fieldset className="border border-neutral-300 rounded p-2">
      <legend className="text-xs font-semibold px-1">{title}</legend>

      {/* Connect / Disconnect */}
      <div className="flex gap-1 mb-2">
        <button
          onClick={handleConnect}
          disabled={state.connected}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
        >
          Connect
        </button>
        <button
          onClick={handleDisconnect}
          disabled={!state.connected}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
        >
          Disconnect
        </button>
        <div className="flex-1" />
      </div>

      {/* Flow rate */}
      <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-1 text-xs mb-2">
        <label className="text-right">Flow Rate:</label>
        <div className="flex gap-1">
          <input
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
            className="flex-1 border border-neutral-400 rounded px-1 py-0.5"
          />
          <select
            value={state.flowUnit}
            onChange={(e) => setState((s) => ({ ...s, flowUnit: e.target.value }))}
            disabled={!state.connected}
            className="border border-neutral-400 rounded px-1 py-0.5"
          >
            <option value="uL/min">uL/min</option>
            <option value="mL/min">mL/min</option>
          </select>
        </div>

        <label className="text-right">Direction:</label>
        <select
          value={state.direction}
          onChange={(e) => {
            const dir = e.target.value as PumpDirection;
            setState((s) => ({ ...s, direction: dir }));
            setPumpDirection(pumpId, dir);
          }}
          disabled={!state.connected}
          className="border border-neutral-400 rounded px-1 py-0.5"
        >
          <option value="infuse">Infuse</option>
          <option value="withdraw">Withdraw</option>
        </select>
      </div>

      {/* Start / Stop / Purge */}
      <div className="flex gap-1 mb-2">
        <button
          onClick={() => startPump(pumpId)}
          disabled={!state.connected}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
        >
          Start
        </button>
        <button
          onClick={() => stopPump(pumpId)}
          disabled={!state.connected}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
        >
          Stop
        </button>
        <button
          onClick={() => purgePump(pumpId, state.direction)}
          disabled={!state.connected}
          className="px-2 py-0.5 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded disabled:opacity-50"
          title="Run pump at full speed (hold to purge)"
        >
          Purge
        </button>
        <div className="flex-1" />
      </div>

      {/* Status */}
      <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-0.5 text-xs">
        <span className="text-right text-neutral-600">Status:</span>
        <span>{state.status}</span>
        <span className="text-right text-neutral-600">Flow:</span>
        <span>{state.flowStatus}</span>
        <span className="text-right text-neutral-600">Volume:</span>
        <span>{state.volume}</span>
      </div>
    </fieldset>
  );
}

export function SyringePumpPanel() {
  return (
    <div className="p-1.5 flex flex-col gap-1" style={{ padding: "var(--spacing-sm)" }}>
      <PumpGroup pumpId="sample" title="Sample Pump" />
      <PumpGroup pumpId="sheath" title="Sheath Pump" />
    </div>
  );
}
