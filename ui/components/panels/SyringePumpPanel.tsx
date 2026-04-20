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
import { Button } from "../ui/button";
import { Input } from "../ui/input";
import { Label } from "../ui/label";
import { Card, CardContent, CardHeader, CardTitle } from "../ui/card";

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
    <Card>
      <CardHeader className="p-3 pb-0">
        <CardTitle>{title}</CardTitle>
      </CardHeader>
      <CardContent className="p-3 flex flex-col gap-2">
        {/* Connect/Disconnect */}
        <div className="flex gap-1">
          <Button size="sm" onClick={handleConnect} disabled={state.connected}>Connect</Button>
          <Button size="sm" variant="outline" onClick={handleDisconnect} disabled={!state.connected}>
            Disconnect
          </Button>
        </div>

        {/* Flow Rate & Direction */}
        <div className="form-grid">
          <Label>Flow Rate:</Label>
          <div className="flex gap-1">
            <Input
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
              className="flex-1"
            />
            <select
              className="select-styled"
              value={state.flowUnit}
              onChange={(e) => setState((s) => ({ ...s, flowUnit: e.target.value }))}
              disabled={!state.connected}
              style={{ width: 90 }}
            >
              <option value="uL/min">uL/min</option>
              <option value="mL/min">mL/min</option>
            </select>
          </div>

          <Label>Direction:</Label>
          <select
            className="select-styled"
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

        {/* Start/Stop/Purge */}
        <div className="flex gap-1">
          <Button size="sm" onClick={() => startPump(pumpId)} disabled={!state.connected}>Start</Button>
          <Button size="sm" variant="outline" onClick={() => stopPump(pumpId)} disabled={!state.connected}>Stop</Button>
          <Button size="sm" variant="outline" onClick={() => purgePump(pumpId, state.direction)} disabled={!state.connected}>
            Purge
          </Button>
        </div>

        {/* Status display */}
        <div className="form-grid">
          <Label>Status:</Label>
          <span className="text-xs">{state.status}</span>
          <Label>Flow:</Label>
          <span className="text-xs">{state.flowStatus}</span>
          <Label>Volume:</Label>
          <span className="text-xs">{state.volume}</span>
        </div>
      </CardContent>
    </Card>
  );
}

export function SyringePumpPanel() {
  return (
    <div className="flex flex-col p-3 gap-2">
      <PumpGroup pumpId="sample" title="Sample Pump" />
      <PumpGroup pumpId="sheath" title="Sheath Pump" />
      <div className="flex-1" />
    </div>
  );
}
