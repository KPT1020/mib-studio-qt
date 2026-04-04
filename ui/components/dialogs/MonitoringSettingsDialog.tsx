import { useState } from "react";

interface Props {
  onClose: () => void;
}

export function MonitoringSettingsDialog({ onClose }: Props) {
  const [kdeBandwidth, setKdeBandwidth] = useState(50.0);
  const [kdeGridResolution, setKdeGridResolution] = useState(50);
  const [scatterXMin, setScatterXMin] = useState(0);
  const [scatterXMax, setScatterXMax] = useState(500);
  const [scatterYMin, setScatterYMin] = useState(0);
  const [scatterYMax, setScatterYMax] = useState(0.1);
  const [histXMin, setHistXMin] = useState(0);
  const [histXMax, setHistXMax] = useState(50);
  const [histYMax, setHistYMax] = useState(100);
  const [histBinWidth, setHistBinWidth] = useState(1.0);

  const handleApply = () => {
    // TODO: Save monitoring settings to backend
  };

  const handleOk = () => {
    handleApply();
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40">
      <div className="bg-[var(--bg-window)] shadow-xl flex flex-col" style={{ width: 400, minHeight: 420 }}>
        <div className="px-3 py-2 border-b border-[var(--border-widget)] font-semibold text-sm">
          Monitoring Settings
        </div>
        <div className="flex-1 overflow-y-auto px-3 py-3 flex flex-col gap-3">
          <div className="qt-form">
            <label>KDE Bandwidth</label>
            <input type="number" min={1} max={1000} step={10} value={kdeBandwidth}
              onChange={(e) => setKdeBandwidth(Number(e.target.value))} className="qt-input"
              title="KDE bandwidth (higher = smoother)" />
            <label>KDE Grid Resolution</label>
            <input type="number" min={10} max={200} step={10} value={kdeGridResolution}
              onChange={(e) => setKdeGridResolution(Number(e.target.value))} className="qt-input"
              title="Grid resolution for KDE heat map" />
          </div>

          <fieldset className="qt-groupbox">
            <legend>Scatter plot axis (Area vs Deformability)</legend>
            <div className="qt-form">
              <label>X (Area) min</label>
              <input type="number" min={0} max={100000} step={0.01} value={scatterXMin}
                onChange={(e) => setScatterXMin(Number(e.target.value))} className="qt-input" />
              <label>X (Area) max</label>
              <input type="number" min={0} max={100000} step={0.01} value={scatterXMax}
                onChange={(e) => setScatterXMax(Number(e.target.value))} className="qt-input" />
              <label>Y (Deformability) min</label>
              <input type="number" min={0} max={10} step={0.001} value={scatterYMin}
                onChange={(e) => setScatterYMin(Number(e.target.value))} className="qt-input" />
              <label>Y (Deformability) max</label>
              <input type="number" min={0} max={10} step={0.001} value={scatterYMax}
                onChange={(e) => setScatterYMax(Number(e.target.value))} className="qt-input" />
            </div>
          </fieldset>

          <fieldset className="qt-groupbox">
            <legend>Histogram axis (Ring width distribution)</legend>
            <div className="qt-form">
              <label>X (Ring width) min</label>
              <input type="number" min={0} max={100} step={0.01} value={histXMin}
                onChange={(e) => setHistXMin(Number(e.target.value))} className="qt-input" />
              <label>X (Ring width) max</label>
              <input type="number" min={0} max={100} step={0.01} value={histXMax}
                onChange={(e) => setHistXMax(Number(e.target.value))} className="qt-input" />
              <label>Y (Frequency) max</label>
              <input type="number" min={1} max={1000000} value={histYMax}
                onChange={(e) => setHistYMax(Number(e.target.value))} className="qt-input" />
              <label>Bin width</label>
              <input type="number" min={0.01} max={10} step={0.1} value={histBinWidth}
                onChange={(e) => setHistBinWidth(Number(e.target.value))} className="qt-input"
                title="Histogram bin width" />
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
