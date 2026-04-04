import type { ProcessedFrame } from "../../types/backend";

interface MetricsTableProps {
  frames: ProcessedFrame[];
  selectedIndex: number | null;
  onSelect: (index: number) => void;
}

const COLUMNS = [
  { key: "index", label: "Index" },
  { key: "area", label: "Area (um²)" },
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
      <table className="w-full text-xs border-collapse">
        <thead className="sticky top-0 bg-neutral-100">
          <tr>
            {COLUMNS.map((col) => (
              <th
                key={col.key}
                className="text-left px-2 py-1 border-b border-neutral-300 font-semibold"
              >
                {col.label}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {frames.map((frame, i) => (
            <tr
              key={frame.index}
              onClick={() => onSelect(i)}
              className={`cursor-pointer ${
                selectedIndex === i
                  ? "bg-blue-100"
                  : i % 2 === 0
                    ? "bg-white"
                    : "bg-neutral-50"
              } hover:bg-blue-50`}
            >
              <td className="px-2 py-0.5 border-b border-neutral-200">{frame.index}</td>
              <td className="px-2 py-0.5 border-b border-neutral-200">
                {frame.validation.area.toFixed(1)}
              </td>
              <td className="px-2 py-0.5 border-b border-neutral-200">
                {frame.validation.deformability.toFixed(4)}
              </td>
              <td className="px-2 py-0.5 border-b border-neutral-200">
                {frame.validation.areaRatio.toFixed(3)}
              </td>
              <td className="px-2 py-0.5 border-b border-neutral-200">
                {frame.validation.ringRatio.toFixed(2)}
              </td>
              <td className="px-2 py-0.5 border-b border-neutral-200">
                {frame.validation.youngsModulus.toFixed(1)}
              </td>
              <td className="px-2 py-0.5 border-b border-neutral-200">
                {frame.validation.isValid ? "Yes" : "No"}
              </td>
              <td className="px-2 py-0.5 border-b border-neutral-200">
                {frame.validation.isTargetGroup ? "Yes" : "No"}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      {frames.length === 0 && (
        <p className="text-center text-xs text-neutral-400 py-4">No data</p>
      )}
    </div>
  );
}
