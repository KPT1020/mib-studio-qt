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
    // TODO: Save pump connection settings via backend
  };

  const handleOk = () => {
    handleApply();
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-[var(--bg-window)] shadow-xl flex flex-col" style={{ width: 450 }}>
        <div className="px-3 py-2 border-b border-[var(--border-widget)] font-semibold text-sm">
          Syringe Pump Settings
        </div>
        <div className="px-3 py-3 flex flex-col gap-3">
          <fieldset className="qt-groupbox">
            <legend>Sample Pump</legend>
            <div className="qt-form">
              <label>COM Port:</label>
              <input type="number" min={1} max={99} value={sampleCom}
                onChange={(e) => setSampleCom(Number(e.target.value))} className="qt-input" />
              <label>Baud Rate:</label>
              <select value={sampleBaud} onChange={(e) => setSampleBaud(Number(e.target.value))} className="qt-select">
                {[9600, 19200, 38400, 57600, 115200].map(r => <option key={r} value={r}>{r}</option>)}
              </select>
              <label>Modbus Address:</label>
              <input type="number" min={1} max={255} value={sampleAddr}
                onChange={(e) => setSampleAddr(Number(e.target.value))} className="qt-input" />
            </div>
          </fieldset>
          <fieldset className="qt-groupbox">
            <legend>Sheath Pump</legend>
            <div className="qt-form">
              <label>COM Port:</label>
              <input type="number" min={1} max={99} value={sheathCom}
                onChange={(e) => setSheathCom(Number(e.target.value))} className="qt-input" />
              <label>Baud Rate:</label>
              <select value={sheathBaud} onChange={(e) => setSheathBaud(Number(e.target.value))} className="qt-select">
                {[9600, 19200, 38400, 57600, 115200].map(r => <option key={r} value={r}>{r}</option>)}
              </select>
              <label>Modbus Address:</label>
              <input type="number" min={1} max={255} value={sheathAddr}
                onChange={(e) => setSheathAddr(Number(e.target.value))} className="qt-input" />
            </div>
          </fieldset>
        </div>
        <div className="flex justify-end gap-2 px-3 py-2 border-t border-[var(--border-widget)]">
          <button onClick={onClose} className="qt-btn">Cancel</button>
          <button onClick={handleApply} className="qt-btn">Apply</button>
          <button onClick={handleOk} className="qt-btn">OK</button>
        </div>
      </div>
    </div>
  );
}
