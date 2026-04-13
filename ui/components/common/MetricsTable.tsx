import type { ProcessedFrame } from "../../types/backend";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "../ui/table";
import { cn } from "@/lib/utils";

interface MetricsTableProps {
  frames: ProcessedFrame[];
  selectedIndex: number | null;
  onSelect: (index: number) => void;
}

const COLUMNS = [
  { key: "index", label: "Index" },
  { key: "area", label: "Area (um\u00B2)" },
  { key: "deformability", label: "Deformability" },
  { key: "areaRatio", label: "Area Ratio" },
  { key: "ringRatio", label: "Ring Ratio" },
  { key: "youngsModulus", label: "E-Modulus" },
  { key: "isValid", label: "Valid" },
  { key: "isTargetGroup", label: "Target" },
] as const;

export function MetricsTable({ frames, selectedIndex, onSelect }: MetricsTableProps) {
  return (
    <div className="h-full overflow-auto">
      <Table>
        <TableHeader>
          <TableRow>
            {COLUMNS.map((col) => (
              <TableHead key={col.key}>{col.label}</TableHead>
            ))}
          </TableRow>
        </TableHeader>
        <TableBody>
          {frames.map((frame, i) => (
            <TableRow
              key={frame.index}
              onClick={() => onSelect(i)}
              className={cn(
                "cursor-pointer",
                selectedIndex === i && "bg-primary/10"
              )}
            >
              <TableCell className="font-mono">{frame.index}</TableCell>
              <TableCell className="font-mono">{frame.validation.area.toFixed(1)}</TableCell>
              <TableCell className="font-mono">{frame.validation.deformability.toFixed(4)}</TableCell>
              <TableCell className="font-mono">{frame.validation.areaRatio.toFixed(3)}</TableCell>
              <TableCell className="font-mono">{frame.validation.ringRatio.toFixed(2)}</TableCell>
              <TableCell className="font-mono">{frame.validation.youngsModulus.toFixed(1)}</TableCell>
              <TableCell>{frame.validation.isValid ? "Yes" : "No"}</TableCell>
              <TableCell>{frame.validation.isTargetGroup ? "Yes" : "No"}</TableCell>
            </TableRow>
          ))}
        </TableBody>
      </Table>
      {frames.length === 0 && (
        <p className="text-center text-xs text-muted-foreground py-4">No data</p>
      )}
    </div>
  );
}
