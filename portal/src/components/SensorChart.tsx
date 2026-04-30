"use client";

import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import type { SensorMeasurements } from "@/lib/api";
import {
  formatChartTime,
  formatValue,
  kindLabel,
  sensorLabel,
} from "@/lib/api";

const SERIES_COLORS = [
  "#0f766e",
  "#2563eb",
  "#b45309",
  "#be123c",
  "#4f46e5",
  "#15803d",
  "#7c3aed",
  "#334155",
];

type SensorChartProps = {
  sensor: SensorMeasurements;
};

type ChartRow = {
  sampledAt: string;
  [kind: string]: number | string | null;
};

export function SensorChart({ sensor }: SensorChartProps) {
  const kinds = sensor.series.map((series) => series.kind);
  const rows = buildRows(sensor);

  return (
    <article className="rounded-md border border-slate-200 bg-white p-4 shadow-sm md:p-5">
      <div className="flex flex-wrap items-start justify-between gap-3">
        <div>
          <p className="text-xs font-semibold uppercase tracking-[0.16em] text-emerald-700">
            Sensor
          </p>
          <h2 className="mt-1 text-lg font-semibold text-slate-950">
            {sensorLabel(sensor.sensor_type)}
          </h2>
        </div>
        <p className="rounded-md bg-slate-100 px-3 py-2 text-sm text-slate-600">
          {kinds.length} series
        </p>
      </div>

      {rows.length === 0 ? (
        <p className="mt-5 rounded-md bg-slate-50 px-4 py-6 text-center text-sm text-slate-600">
          No points for this period.
        </p>
      ) : (
        <div className="mt-5 h-[320px]">
          <ResponsiveContainer height="100%" width="100%">
            <LineChart
              data={rows}
              margin={{ bottom: 10, left: 4, right: 20, top: 12 }}
            >
              <CartesianGrid stroke="#e2e8f0" strokeDasharray="3 3" />
              <XAxis
                dataKey="sampledAt"
                minTickGap={24}
                stroke="#64748b"
                tickFormatter={formatChartTime}
                tickMargin={10}
              />
              <YAxis
                allowDecimals
                stroke="#64748b"
                tickMargin={8}
                width={56}
              />
              <Tooltip
                formatter={(value, name) => {
                  const kind = String(name);
                  return [
                    typeof value === "number"
                      ? formatValue(kind, value)
                      : String(value),
                    kindLabel(kind),
                  ];
                }}
                labelFormatter={(label) => formatChartTime(String(label))}
              />
              <Legend formatter={(value) => kindLabel(String(value))} />
              {kinds.map((kind, index) => (
                <Line
                  connectNulls
                  dataKey={kind}
                  dot={false}
                  isAnimationActive={false}
                  key={kind}
                  name={kind}
                  stroke={SERIES_COLORS[index % SERIES_COLORS.length]}
                  strokeWidth={2}
                  type="monotone"
                />
              ))}
            </LineChart>
          </ResponsiveContainer>
        </div>
      )}
    </article>
  );
}

function buildRows(sensor: SensorMeasurements): ChartRow[] {
  const rows = new Map<string, ChartRow>();

  for (const series of sensor.series) {
    for (const point of series.points) {
      const row = rows.get(point.t) ?? { sampledAt: point.t };
      row[series.kind] = point.v;
      rows.set(point.t, row);
    }
  }

  return Array.from(rows.values()).sort(
    (a, b) =>
      new Date(a.sampledAt).getTime() - new Date(b.sampledAt).getTime(),
  );
}
