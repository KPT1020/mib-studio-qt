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
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-w-md">
        <DialogHeader>
          <DialogTitle>Syringe Pump Settings</DialogTitle>
          <DialogDescription>Configure pump connection parameters.</DialogDescription>
        </DialogHeader>

        <div className="space-y-3">
          <Card>
            <CardHeader className="p-3 pb-0">
              <CardTitle>Sample Pump</CardTitle>
            </CardHeader>
            <CardContent className="p-3">
              <div className="form-grid">
                <Label>COM Port:</Label>
                <Input type="number" min={1} max={99} value={sampleCom}
                  onChange={(e) => setSampleCom(Number(e.target.value))} />
                <Label>Baud Rate:</Label>
                <select className="select-styled" value={sampleBaud}
                  onChange={(e) => setSampleBaud(Number(e.target.value))}>
                  {[9600, 19200, 38400, 57600, 115200].map(r => <option key={r} value={r}>{r}</option>)}
                </select>
                <Label>Modbus Address:</Label>
                <Input type="number" min={1} max={255} value={sampleAddr}
                  onChange={(e) => setSampleAddr(Number(e.target.value))} />
              </div>
            </CardContent>
          </Card>

          <Card>
            <CardHeader className="p-3 pb-0">
              <CardTitle>Sheath Pump</CardTitle>
            </CardHeader>
            <CardContent className="p-3">
              <div className="form-grid">
                <Label>COM Port:</Label>
                <Input type="number" min={1} max={99} value={sheathCom}
                  onChange={(e) => setSheathCom(Number(e.target.value))} />
                <Label>Baud Rate:</Label>
                <select className="select-styled" value={sheathBaud}
                  onChange={(e) => setSheathBaud(Number(e.target.value))}>
                  {[9600, 19200, 38400, 57600, 115200].map(r => <option key={r} value={r}>{r}</option>)}
                </select>
                <Label>Modbus Address:</Label>
                <Input type="number" min={1} max={255} value={sheathAddr}
                  onChange={(e) => setSheathAddr(Number(e.target.value))} />
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
