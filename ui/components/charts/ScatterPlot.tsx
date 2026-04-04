import {
  ScatterChart,
  Scatter,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  Legend,
} from "recharts";

interface ScatterDataPoint {
  area: number;
  deformability: number;
  isTargetGroup: boolean;
}

interface ScatterPlotProps {
  data: ScatterDataPoint[];
}

export function ScatterPlot({ data }: ScatterPlotProps) {
  const validData = data.filter((d) => !d.isTargetGroup);
  const targetData = data.filter((d) => d.isTargetGroup);

  return (
    <div className="w-full h-full">
      <ResponsiveContainer width="100%" height="100%">
        <ScatterChart margin={{ top: 10, right: 10, bottom: 30, left: 40 }}>
          <CartesianGrid strokeDasharray="3 3" />
          <XAxis
            type="number"
            dataKey="area"
            name="Area"
            unit=" um²"
            label={{ value: "Area (um²)", position: "bottom", offset: 10 }}
            fontSize={11}
          />
          <YAxis
            type="number"
            dataKey="deformability"
            name="Deformability"
            label={{
              value: "Deformability",
              angle: -90,
              position: "insideLeft",
              offset: -25,
            }}
            fontSize={11}
          />
          <Tooltip cursor={{ strokeDasharray: "3 3" }} />
          <Legend verticalAlign="top" />
          <Scatter
            name="Valid"
            data={validData}
            fill="var(--color-valid, #00ff00)"
            opacity={0.6}
            r={3}
          />
          <Scatter
            name="Target"
            data={targetData}
            fill="var(--color-target, #0078ff)"
            opacity={0.8}
            r={4}
          />
        </ScatterChart>
      </ResponsiveContainer>
    </div>
  );
}
