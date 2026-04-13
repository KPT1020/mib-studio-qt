import { useAppStore } from "../../stores/appStore";
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from "../ui/dialog";
import { Button } from "../ui/button";

interface Props {
  onClose: () => void;
}

export function FrameViewerDialog({ onClose }: Props) {
  const frame = useAppStore((s) => s.frameViewerFrame);

  if (!frame) return null;

  return (
    <Dialog open onOpenChange={(open) => !open && onClose()}>
      <DialogContent className="max-w-[90vw] max-h-[90vh] flex flex-col">
        <DialogHeader>
          <DialogTitle>Frame #{frame.index}</DialogTitle>
        </DialogHeader>

        <div className="flex-1 min-h-0 overflow-auto bg-black flex items-center justify-center rounded-md">
          <img
            src={`data:image/jpeg;base64,${frame.imageBase64}`}
            alt={`Frame ${frame.index}`}
            className="max-w-full max-h-full object-contain"
            style={{ imageRendering: "pixelated" }}
          />
        </div>

        <DialogFooter>
          <Button onClick={onClose}>Close</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
