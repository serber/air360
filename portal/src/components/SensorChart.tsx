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
import type { KindMeasurements } from "@/lib/api";
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
  measurement: ChartMeasurement;
};

type ChartRow = {
  sampledAt: string;
  [sensorType: string]: number | string | null;
};

export type ChartSeries = KindMeasurements["series"][number] & {
  chartKey?: string;
  kind?: string;
  label?: string;
};

export type ChartMeasurement = Omit<KindMeasurements, "series"> & {
  series: ChartSeries[];
  title?: string;
};

export function SensorChart({ measurement }: SensorChartProps) {
  const seriesKeys = measurement.series.map(chartSeriesKey);
  const seriesByKey = new Map(
    measurement.series.map((series) => [chartSeriesKey(series), series]),
  );
  const rows = buildRows(measurement);

  return (
    <article className="air-chart-card">
      <div className="air-chart-head">
        <div className="air-chart-head-left">
          <span className="air-chart-source">Measurement</span>
          <div>
            {measurement.title ?? kindLabel(measurement.kind)}
          </div>
        </div>
        <div className="air-chart-stats">
          {seriesKeys.length}{" "}
          {seriesKeys.length === 1 ? "source" : "sources"}
        </div>
      </div>

      {rows.length === 0 ? (
        <p className="air-chart-empty">
          No points for this period.
        </p>
      ) : (
        <div className="air-chart-body">
          <ResponsiveContainer height="100%" width="100%">
            <LineChart
              data={rows}
              margin={{ bottom: 10, left: 4, right: 20, top: 12 }}
            >
              <CartesianGrid stroke="var(--line-2)" strokeDasharray="3 3" />
              <XAxis
                dataKey="sampledAt"
                minTickGap={24}
                stroke="var(--ink-3)"
                tickFormatter={formatChartTime}
                tickMargin={10}
              />
              <YAxis
                allowDecimals
                stroke="var(--ink-3)"
                tickMargin={8}
                width={56}
              />
              <Tooltip
                formatter={(value, name) => [
                  typeof value === "number"
                    ? formatValue(
                        seriesByKey.get(String(name))?.kind ?? measurement.kind,
                        value,
                      )
                    : String(value),
                  seriesLabel(seriesByKey.get(String(name))),
                ]}
                labelFormatter={(label) => formatChartTime(String(label))}
              />
              <Legend
                formatter={(value) =>
                  seriesLabel(seriesByKey.get(String(value)))
                }
              />
              {seriesKeys.map((seriesKey, index) => (
                <Line
                  connectNulls
                  dataKey={seriesKey}
                  dot={false}
                  isAnimationActive={false}
                  key={seriesKey}
                  name={seriesKey}
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

function buildRows(measurement: ChartMeasurement): ChartRow[] {
  const rows = new Map<string, ChartRow>();

  for (const series of measurement.series) {
    const seriesKey = chartSeriesKey(series);

    for (const point of series.points) {
      const key = String(point.t);
      const row = rows.get(key) ?? { sampledAt: key };
      row[seriesKey] = point.v;
      rows.set(key, row);
    }
  }

  return Array.from(rows.values()).sort(
    (a, b) =>
      new Date(a.sampledAt).getTime() - new Date(b.sampledAt).getTime(),
  );
}

function chartSeriesKey(series: ChartSeries): string {
  return series.chartKey ?? series.sensor_type;
}

function seriesLabel(series: ChartSeries | undefined): string {
  if (!series) {
    return "";
  }

  return series.label ?? sensorLabel(series.sensor_type);
}
