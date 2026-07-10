"use client";

import {
  Chart,
  Legend,
  LineController,
  LineElement,
  LinearScale,
  PointElement,
  TimeScale,
  Tooltip,
  type ChartConfiguration,
  type Plugin,
} from "chart.js";
import "chartjs-adapter-date-fns";
import { useEffect, useRef } from "react";
import { useTranslations } from "next-intl";
import type { KindMeasurements } from "@/lib/api";
import {
  formatChartTime,
  formatValue,
  kindLabel,
  kindUnit,
  sensorLabel,
} from "@/lib/api";
import { usePortalTheme, type PortalTheme } from "@/lib/theme";

Chart.register(
  Legend,
  LineController,
  LineElement,
  LinearScale,
  PointElement,
  TimeScale,
  Tooltip,
);

// Validated categorical palette: fixed slot order, never cycled through generated
// hues. Both columns were checked against the portal panel surfaces (#ffffff and
// #14191a) for the lightness band, chroma floor, CVD separation, and contrast.
const SERIES_COLORS: Record<PortalTheme, readonly string[]> = {
  light: [
    "#2a78d6",
    "#1baf7a",
    "#eda100",
    "#008300",
    "#4a3aa7",
    "#e34948",
    "#e87ba4",
    "#eb6834",
  ],
  dark: [
    "#3987e5",
    "#199e70",
    "#c98500",
    "#008300",
    "#9085e9",
    "#e66767",
    "#d55181",
    "#d95926",
  ],
};

const HOUR_MS = 60 * 60 * 1000;
const DAY_MS = 24 * HOUR_MS;

type SensorChartProps = {
  measurement: ChartMeasurement;
};

export type ChartSeries = KindMeasurements["series"][number] & {
  kind?: string;
  label?: string;
};

export type ChartMeasurement = Omit<KindMeasurements, "series"> & {
  series: ChartSeries[];
  title?: string;
};

type ChartTokens = {
  fontFamily: string;
  grid: string;
  ink: string;
  ink2: string;
  ink3: string;
  line: string;
  surface: string;
};

/**
 * Vertical hairline under the tooltip: readers aim at a timestamp, not at a 2px line.
 */
function createCrosshairPlugin(color: string): Plugin<"line"> {
  return {
    id: "air360Crosshair",
    afterDatasetsDraw(chart) {
      const active = chart.tooltip?.getActiveElements() ?? [];

      if (active.length === 0) {
        return;
      }

      const { ctx, chartArea } = chart;
      const x = active[0].element.x;

      ctx.save();
      ctx.beginPath();
      ctx.lineWidth = 1;
      ctx.strokeStyle = color;
      ctx.moveTo(x, chartArea.top);
      ctx.lineTo(x, chartArea.bottom);
      ctx.stroke();
      ctx.restore();
    },
  };
}

export function SensorChart({ measurement }: SensorChartProps) {
  const t = useTranslations("chart");
  const theme = usePortalTheme();
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const timestamps = collectTimestamps(measurement);
  const sourceCount = measurement.series.length;

  useEffect(() => {
    const canvas = canvasRef.current;

    if (!canvas) {
      return;
    }

    const chart = new Chart(canvas, buildConfig(measurement, theme, readTokens()));

    return () => chart.destroy();
  }, [measurement, theme]);

  return (
    <article className="air-chart-card">
      <div className="air-chart-head">
        <div className="air-chart-head-left">
          <span className="air-chart-source">{t("source")}</span>
          <div>{measurement.title ?? kindLabel(measurement.kind)}</div>
        </div>
        <div className="air-chart-stats">
          {t("sourceCount", { count: sourceCount })}
        </div>
      </div>

      {timestamps.length === 0 ? (
        <p className="air-chart-empty">{t("empty")}</p>
      ) : (
        <div className="air-chart-body">
          <canvas
            aria-label={measurement.title ?? kindLabel(measurement.kind)}
            ref={canvasRef}
            role="img"
          />
        </div>
      )}
    </article>
  );
}

function buildConfig(
  measurement: ChartMeasurement,
  theme: PortalTheme,
  tokens: ChartTokens,
): ChartConfiguration<"line", (number | null)[], number> {
  const labels = collectTimestamps(measurement);
  const palette = SERIES_COLORS[theme];
  const unit = kindUnit(measurement.kind);
  const timeAxis = pickTimeAxis(labels);

  return {
    type: "line",
    data: {
      labels,
      datasets: measurement.series.map((series, index) => {
        const color = palette[index % palette.length];

        return {
          borderColor: color,
          borderWidth: 2,
          cubicInterpolationMode: "monotone",
          data: alignPoints(series, labels),
          label: seriesLabel(series),
          pointBackgroundColor: color,
          // Legend and tooltip keys inherit the point's border color, so it carries the
          // series hue; the surface ring lives on the hover state, where markers show.
          pointBorderColor: color,
          pointBorderWidth: 2,
          pointHitRadius: 0,
          pointHoverBorderColor: tokens.surface,
          pointHoverBorderWidth: 2,
          pointHoverRadius: 4,
          pointRadius: 0,
          spanGaps: true,
        };
      }),
    },
    options: {
      animation: false,
      interaction: { axis: "x", intersect: false, mode: "index" },
      maintainAspectRatio: false,
      normalized: true,
      responsive: true,
      resizeDelay: 100,
      scales: {
        x: {
          border: { color: tokens.line },
          grid: { display: false },
          // Chart.js otherwise auto-picks the finest unit and thins it, so ticks drift
          // off the clock (07:35, 08:38, 09:41...). A pinned unit keeps them aligned.
          time: { unit: timeAxis.unit },
          ticks: {
            color: tokens.ink3,
            font: { family: tokens.fontFamily, size: 11 },
            maxRotation: 0,
            stepSize: timeAxis.stepSize,
            callback: (value) => timeAxis.format.format(Number(value)),
          },
          type: "time",
        },
        y: {
          border: { display: false },
          grid: { color: tokens.grid, drawTicks: false },
          ticks: {
            color: tokens.ink3,
            font: { family: tokens.fontFamily, size: 11 },
            padding: 8,
            callback: (value) => formatTickNumber(Number(value)),
          },
          title: unit
            ? {
                color: tokens.ink3,
                display: true,
                font: { family: tokens.fontFamily, size: 11 },
                text: unit,
              }
            : undefined,
        },
      },
      plugins: {
        legend: {
          // A single series is already named by the card title.
          display: measurement.series.length > 1,
          labels: {
            boxHeight: 8,
            color: tokens.ink2,
            font: { family: tokens.fontFamily, size: 12 },
            pointStyle: "line",
            pointStyleWidth: 24,
            usePointStyle: true,
          },
          position: "bottom",
        },
        tooltip: {
          backgroundColor: tokens.surface,
          bodyColor: tokens.ink,
          bodyFont: { family: tokens.fontFamily, size: 12 },
          borderColor: tokens.line,
          borderWidth: 1,
          boxHeight: 12,
          boxWidth: 20,
          boxPadding: 6,
          cornerRadius: 6,
          multiKeyBackground: tokens.surface,
          padding: 10,
          titleColor: tokens.ink3,
          titleFont: { family: tokens.fontFamily, size: 11, weight: "normal" },
          usePointStyle: true,
          callbacks: {
            label: (item) => {
              const series = measurement.series[item.datasetIndex];
              const kind = series?.kind ?? measurement.kind;

              // Values lead, labels follow: the reader has the series, wants the number.
              return `${formatValue(kind, Number(item.parsed.y))} · ${item.dataset.label ?? ""}`;
            },
            labelPointStyle: () => ({ pointStyle: "line", rotation: 0 }),
            title: (items) =>
              items.length > 0 ? formatChartTime(Number(items[0].parsed.x)) : "",
          },
          filter: (item) => Number.isFinite(item.parsed.y),
        },
      },
    },
    plugins: [createCrosshairPlugin(tokens.line)],
  };
}

function readTokens(): ChartTokens {
  const styles = getComputedStyle(document.documentElement);
  const token = (name: string) => styles.getPropertyValue(name).trim();

  return {
    fontFamily: token("--air-font-text"),
    grid: token("--line-2"),
    ink: token("--ink"),
    ink2: token("--ink-2"),
    ink3: token("--ink-3"),
    line: token("--line"),
    surface: token("--panel"),
  };
}

function collectTimestamps(measurement: ChartMeasurement): number[] {
  const timestamps = new Set<number>();

  for (const series of measurement.series) {
    for (const point of series.points) {
      const time = new Date(point.t).getTime();

      if (!Number.isNaN(time)) {
        timestamps.add(time);
      }
    }
  }

  return Array.from(timestamps).sort((a, b) => a - b);
}

function alignPoints(
  series: ChartSeries,
  labels: number[],
): (number | null)[] {
  const values = new Map<number, number>();

  for (const point of series.points) {
    values.set(new Date(point.t).getTime(), point.v);
  }

  return labels.map((label) => values.get(label) ?? null);
}

type TimeAxis = {
  format: Intl.DateTimeFormat;
  stepSize: number;
  unit: "minute" | "hour" | "day" | "month";
};

const CLOCK_FORMAT = { hour: "2-digit", minute: "2-digit" } as const;
const DATE_FORMAT = { day: "numeric", month: "short" } as const;
const MONTH_FORMAT = { month: "short", year: "numeric" } as const;

/**
 * Pins the tick unit and step to the visible span. Left on auto, Chart.js picks the
 * finest unit that fits and lets autoSkip thin it, which walks the labels off the clock.
 */
function pickTimeAxis(labels: number[]): TimeAxis {
  const span =
    labels.length > 1 ? labels[labels.length - 1] - labels[0] : HOUR_MS;
  const clock = new Intl.DateTimeFormat(undefined, CLOCK_FORMAT);

  if (span <= 90 * 60 * 1000) {
    return { format: clock, stepSize: 10, unit: "minute" };
  }

  if (span <= 6 * HOUR_MS) {
    return { format: clock, stepSize: 30, unit: "minute" };
  }

  if (span <= 18 * HOUR_MS) {
    return { format: clock, stepSize: 2, unit: "hour" };
  }

  if (span <= 36 * HOUR_MS) {
    return { format: clock, stepSize: 3, unit: "hour" };
  }

  const date = new Intl.DateTimeFormat(undefined, DATE_FORMAT);

  if (span <= 8 * DAY_MS) {
    return { format: date, stepSize: 1, unit: "day" };
  }

  if (span <= 40 * DAY_MS) {
    return { format: date, stepSize: 5, unit: "day" };
  }

  if (span <= 100 * DAY_MS) {
    return { format: date, stepSize: 10, unit: "day" };
  }

  const month = new Intl.DateTimeFormat(undefined, MONTH_FORMAT);

  if (span <= 200 * DAY_MS) {
    return { format: month, stepSize: 1, unit: "month" };
  }

  return { format: month, stepSize: 2, unit: "month" };
}

function formatTickNumber(value: number): string {
  return new Intl.NumberFormat(undefined, {
    maximumFractionDigits: Math.abs(value) >= 100 ? 0 : 2,
  }).format(value);
}

function seriesLabel(series: ChartSeries): string {
  return series.label ?? sensorLabel(series.sensor_type);
}
