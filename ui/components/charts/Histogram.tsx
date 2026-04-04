import {
  BarChart,
  Bar,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
} from "recharts";
import { useMemo } from "react";

interface HistogramProps {
  data: number[];
  bins?: number;
}

interface BinData {
  label: string;
  count: number;
  min: number;
  max: number;
}

function computeBins(values: number[], numBins: number): BinData[] {
  if (values.length === 0) return [];

  const min = Math.min(...values);
  const max = Math.max(...values);
  const range = max - min || 1;
  const binWidth = range / numBins;

  const bins: BinData[] = Array.from({ length: numBins }, (_, i) => ({
    label: (min + (i + 0.5) * binWidth).toFixed(1),
    count: 0,
    min: min + i * binWidth,
    max: min + (i + 1) * binWidth,
  }));

  for (const v of values) {
    const idx = Math.min(Math.floor((v - min) / binWidth), numBins - 1);
    bins[idx].count++;
  }

  return bins;
}

export function Histogram({ data, bins = 20 }: HistogramProps) {
  const binData = useMemo(() => computeBins(data, bins), [data, bins]);

  return (
    <div className="w-full h-full">
      <ResponsiveContainer width="100%" height="100%">
        <BarChart data={binData} margin={{ top: 10, right: 10, bottom: 30, left: 40 }}>
          <CartesianGrid strokeDasharray="3 3" />
          <XAxis
            dataKey="label"
            fontSize={11}
            label={{ value: "Ring Width", position: "bottom", offset: 10 }}
          />
          <YAxis
            fontSize={11}
            label={{
              value: "Frequency",
              angle: -90,
              position: "insideLeft",
              offset: -25,
            }}
          />
          <Tooltip
            formatter={(value: number) => [value, "Count"]}
            labelFormatter={(label: string) => `Ring Width: ${label}`}
          />
          <Bar dataKey="count" fill="#6366f1" opacity={0.8} />
        </BarChart>
      </ResponsiveContainer>
    </div>
  );
}
