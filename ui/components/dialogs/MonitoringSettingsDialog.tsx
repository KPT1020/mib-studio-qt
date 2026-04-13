import { useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
  DialogDescription,
} from "../ui/dialog";
import { Button } from "../ui/button";
import { Input } from "../ui/input";
import { Label } from "../ui/label";
import { Card, CardContent, CardHeader, CardTitle } from "../ui/card";

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
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-w-md">
        <DialogHeader>
          <DialogTitle>Monitoring Settings</DialogTitle>
          <DialogDescription>Configure chart axes and KDE parameters.</DialogDescription>
        </DialogHeader>

        <div className="space-y-3 max-h-[60vh] overflow-y-auto pr-1">
          <div className="form-grid">
            <Label>KDE Bandwidth</Label>
            <Input type="number" min={1} max={1000} step={10} value={kdeBandwidth}
              onChange={(e) => setKdeBandwidth(Number(e.target.value))}
              title="KDE bandwidth (higher = smoother)" />
            <Label>KDE Grid Resolution</Label>
            <Input type="number" min={10} max={200} step={10} value={kdeGridResolution}
              onChange={(e) => setKdeGridResolution(Number(e.target.value))}
              title="Grid resolution for KDE heat map" />
          </div>

          <Card>
            <CardHeader className="p-3 pb-0">
              <CardTitle>Scatter plot axis (Area vs Deformability)</CardTitle>
            </CardHeader>
            <CardContent className="p-3">
              <div className="form-grid">
                <Label>X (Area) min</Label>
                <Input type="number" min={0} max={100000} step={0.01} value={scatterXMin}
                  onChange={(e) => setScatterXMin(Number(e.target.value))} />
                <Label>X (Area) max</Label>
                <Input type="number" min={0} max={100000} step={0.01} value={scatterXMax}
                  onChange={(e) => setScatterXMax(Number(e.target.value))} />
                <Label>Y (Deform) min</Label>
                <Input type="number" min={0} max={10} step={0.001} value={scatterYMin}
                  onChange={(e) => setScatterYMin(Number(e.target.value))} />
                <Label>Y (Deform) max</Label>
                <Input type="number" min={0} max={10} step={0.001} value={scatterYMax}
                  onChange={(e) => setScatterYMax(Number(e.target.value))} />
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader className="p-3 pb-0">
              <CardTitle>Histogram axis (Ring width distribution)</CardTitle>
            </CardHeader>
            <CardContent className="p-3">
              <div className="form-grid">
                <Label>X (Ring width) min</Label>
                <Input type="number" min={0} max={100} step={0.01} value={histXMin}
                  onChange={(e) => setHistXMin(Number(e.target.value))} />
                <Label>X (Ring width) max</Label>
                <Input type="number" min={0} max={100} step={0.01} value={histXMax}
                  onChange={(e) => setHistXMax(Number(e.target.value))} />
                <Label>Y (Frequency) max</Label>
                <Input type="number" min={1} max={1000000} value={histYMax}
                  onChange={(e) => setHistYMax(Number(e.target.value))} />
                <Label>Bin width</Label>
                <Input type="number" min={0.01} max={10} step={0.1} value={histBinWidth}
                  onChange={(e) => setHistBinWidth(Number(e.target.value))}
                  title="Histogram bin width" />
              </div>
            </CardContent>
          </Card>
        </div>

        <DialogFooter>
          <Button variant="outline" onClick={onClose}>Cancel</Button>
          <Button variant="secondary" onClick={handleApply}>Apply</Button>
          <Button onClick={handleOk}>OK</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
