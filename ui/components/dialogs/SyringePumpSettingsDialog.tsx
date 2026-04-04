import { useState } from "react";

interface Props {
  onClose: () => void;
}

export function SyringePumpSettingsDialog({ onClose }: Props) {
  const [sampleCom, setSampleCom] = useState(1);
  const [sampleBaud, setSampleBaud] = useState(9600);
  const [sampleAddr, setSampleAddr] = useState(1);
  const [sheathCom, setSheathCom] = useState(2);
  const [sheathBaud, setSheathBaud] = useState(9600);
  const [sheathAddr, setSheathAddr] = useState(1);

  const handleApply = () => {
    // TODO: Save pump settings
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-white rounded-lg shadow-xl w-[450px] flex flex-col">
        <div className="px-4 py-3 border-b border-neutral-300 font-semibold text-sm">
          Syringe Pump Settings
        </div>
        <div className="px-4 py-3 space-y-4">
          <fieldset className="border border-neutral-300 rounded p-2">
            <legend className="text-xs font-semibold px-1">Sample Pump</legend>
            <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 text-xs">
              <label className="text-right">COM Port:</label>
              <input type="number" min={1} max={99} value={sampleCom} onChange={(e) => setSampleCom(Number(e.target.value))} className="border border-neutral-400 rounded px-1 py-0.5" />
              <label className="text-right">Baud Rate:</label>
              <select value={sampleBaud} onChange={(e) => setSampleBaud(Number(e.target.value))} className="border border-neutral-400 rounded px-1 py-0.5">
                {[9600, 19200, 38400, 57600, 115200].map(r => <option key={r} value={r}>{r}</option>)}
              </select>
              <label className="text-right">Modbus Address:</label>
              <input type="number" min={1} max={255} value={sampleAddr} onChange={(e) => setSampleAddr(Number(e.target.value))} className="border border-neutral-400 rounded px-1 py-0.5" />
            </div>
          </fieldset>
          <fieldset className="border border-neutral-300 rounded p-2">
            <legend className="text-xs font-semibold px-1">Sheath Pump</legend>
            <div className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-1 text-xs">
              <label className="text-right">COM Port:</label>
              <input type="number" min={1} max={99} value={sheathCom} onChange={(e) => setSheathCom(Number(e.target.value))} className="border border-neutral-400 rounded px-1 py-0.5" />
              <label className="text-right">Baud Rate:</label>
              <select value={sheathBaud} onChange={(e) => setSheathBaud(Number(e.target.value))} className="border border-neutral-400 rounded px-1 py-0.5">
                {[9600, 19200, 38400, 57600, 115200].map(r => <option key={r} value={r}>{r}</option>)}
              </select>
              <label className="text-right">Modbus Address:</label>
              <input type="number" min={1} max={255} value={sheathAddr} onChange={(e) => setSheathAddr(Number(e.target.value))} className="border border-neutral-400 rounded px-1 py-0.5" />
            </div>
          </fieldset>
        </div>
        <div className="flex justify-end gap-2 px-4 py-3 border-t border-neutral-300">
          <button onClick={onClose} className="px-3 py-1 text-xs bg-neutral-200 hover:bg-neutral-300 border border-neutral-400 rounded">Cancel</button>
          <button onClick={handleApply} className="px-3 py-1 text-xs bg-blue-600 text-white rounded hover:bg-blue-700">Apply</button>
        </div>
      </div>
    </div>
  );
}
