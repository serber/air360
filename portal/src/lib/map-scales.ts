import type { DeviceReading, DeviceSummary } from "@/lib/api";

/**
 * Single source of truth for how a measurement value maps to a marker color on
 * every map surface (device map markers and clusters, home-page preview). Each
 * metric is an ordered list of stops; a value takes the first stop whose `max`
 * it does not exceed, and the last stop is open-ended.
 *
 * Adding a metric means one entry in `MAP_METRICS`, one scale here, and a
 * `mapPage.metrics.<metric>` label in the message files.
 */

export const MAP_METRICS = [
  "humidity_percent",
  "pressure_hpa",
  "temperature_c",
  "co2_ppm",
  "pm10_0_ug_m3",
  "pm1_0_ug_m3",
  "pm2_5_ug_m3",
  "pm4_0_ug_m3",
  "dust_concentration_pcs_0_01cf",
  "illuminance_lux",
] as const;

export type MapMetric = (typeof MAP_METRICS)[number];

export type MarkerColors = {
  color: string;
  ring: string;
};

export type ColorStop = MarkerColors & {
  /** Upper bound (inclusive) of this band; the last stop uses `Infinity`. */
  max: number;
  /** Literal legend text such as `"10..18"`. */
  label?: string;
  /** Key under `mapPage.legend` for a translated band name (dust levels). */
  labelKey?: string;
  /** Range text shown after a translated band name. */
  range?: string;
};

export type MetricScale = {
  metric: MapMetric;
  /** Literal legend title (`"PM2.5"`, `"CO2"`); otherwise `titleKey` is used. */
  title?: string;
  /** Key under `mapPage.legend` for the legend title. */
  titleKey?: string;
  /** Key under `mapPage.legend` for the low-to-high hint. */
  hintKey: string;
  /** Legend grid columns. */
  columns: 2 | 3 | 4;
  stops: readonly ColorStop[];
  /** Optional unit line and note under the grid. */
  footnote?: { unit: string; noteKey: string };
};

export const NO_DATA_COLORS: MarkerColors = { color: "#64748b", ring: "#e2e8f0" };
export const STALE_DEVICE_COLORS: MarkerColors = { color: "#6b7280", ring: "#d1d5db" };

const TEMPERATURE_SCALE: MetricScale = {
  metric: "temperature_c",
  titleKey: "temperature",
  hintKey: "coldToHot",
  columns: 4,
  stops: [
    { label: "< -20", max: -20, color: "#312e81", ring: "#c7d2fe" },
    { label: "-20..-10", max: -10, color: "#1e3a8a", ring: "#bfdbfe" },
    { label: "-10..0", max: 0, color: "#2563eb", ring: "#bfdbfe" },
    { label: "0..10", max: 10, color: "#0891b2", ring: "#a5f3fc" },
    { label: "10..18", max: 18, color: "#0f766e", ring: "#99f6e4" },
    { label: "18..24", max: 24, color: "#16a34a", ring: "#bbf7d0" },
    { label: "24..28", max: 28, color: "#ca8a04", ring: "#fef08a" },
    { label: "28..32", max: 32, color: "#ea580c", ring: "#fed7aa" },
    { label: "32..40", max: 40, color: "#be123c", ring: "#fecdd3" },
    { label: "> 40", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
  ],
};

const PRESSURE_SCALE: MetricScale = {
  metric: "pressure_hpa",
  titleKey: "pressure",
  hintKey: "lowToHigh",
  columns: 3,
  stops: [
    { label: "< 980", max: 980, color: "#4338ca", ring: "#c7d2fe" },
    { label: "980..1000", max: 1000, color: "#2563eb", ring: "#bfdbfe" },
    { label: "1000..1010", max: 1010, color: "#0891b2", ring: "#a5f3fc" },
    { label: "1010..1020", max: 1020, color: "#16a34a", ring: "#bbf7d0" },
    { label: "1020..1030", max: 1030, color: "#ca8a04", ring: "#fef08a" },
    { label: "1030..1040", max: 1040, color: "#ea580c", ring: "#fed7aa" },
    { label: "> 1040", max: Infinity, color: "#be123c", ring: "#fecdd3" },
  ],
};

const HUMIDITY_SCALE: MetricScale = {
  metric: "humidity_percent",
  titleKey: "humidity",
  hintKey: "dryToHumid",
  columns: 3,
  stops: [
    { label: "< 20", max: 20, color: "#ea580c", ring: "#fed7aa" },
    { label: "20..30", max: 30, color: "#ca8a04", ring: "#fef08a" },
    { label: "30..60", max: 60, color: "#15803d", ring: "#bbf7d0" },
    { label: "60..75", max: 75, color: "#0f766e", ring: "#99f6e4" },
    { label: "75..90", max: 90, color: "#2563eb", ring: "#bfdbfe" },
    { label: "> 90", max: Infinity, color: "#4338ca", ring: "#c7d2fe" },
  ],
};

const CO2_SCALE: MetricScale = {
  metric: "co2_ppm",
  title: "CO2",
  hintKey: "baselineToHigh",
  columns: 3,
  stops: [
    { label: "< 380", max: 380, color: "#7c3aed", ring: "#ddd6fe" },
    { label: "380..450", max: 450, color: "#0f766e", ring: "#99f6e4" },
    { label: "450..600", max: 600, color: "#15803d", ring: "#bbf7d0" },
    { label: "600..800", max: 800, color: "#ca8a04", ring: "#fef08a" },
    { label: "800..1000", max: 1000, color: "#ea580c", ring: "#fed7aa" },
    { label: "> 1000", max: Infinity, color: "#be123c", ring: "#fecdd3" },
  ],
};

// PM bands follow the WHO 2021 guideline steps for the low end and the US EPA
// AQI breakpoints above that.
const PM25_SCALE: MetricScale = {
  metric: "pm2_5_ug_m3",
  title: "PM2.5",
  hintKey: "cleanToHazardous",
  columns: 3,
  stops: [
    { label: "<= 5", max: 5, color: "#047857", ring: "#a7f3d0" },
    { label: "5..9", max: 9, color: "#16a34a", ring: "#bbf7d0" },
    { label: "9.1..15", max: 15, color: "#ca8a04", ring: "#fef08a" },
    { label: "15.1..35.4", max: 35.4, color: "#ea580c", ring: "#fed7aa" },
    { label: "35.5..55.4", max: 55.4, color: "#dc2626", ring: "#fecaca" },
    { label: "55.5..125.4", max: 125.4, color: "#be123c", ring: "#fecdd3" },
    { label: "125.5..225.4", max: 225.4, color: "#7e22ce", ring: "#e9d5ff" },
    { label: "> 225.5", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
  ],
};

const PM10_SCALE: MetricScale = {
  metric: "pm10_0_ug_m3",
  title: "PM10",
  hintKey: "cleanToHazardous",
  columns: 3,
  stops: [
    { label: "<= 15", max: 15, color: "#047857", ring: "#a7f3d0" },
    { label: "15..45", max: 45, color: "#16a34a", ring: "#bbf7d0" },
    { label: "45..54", max: 54, color: "#ca8a04", ring: "#fef08a" },
    { label: "55..154", max: 154, color: "#ea580c", ring: "#fed7aa" },
    { label: "155..254", max: 254, color: "#dc2626", ring: "#fecaca" },
    { label: "255..354", max: 354, color: "#be123c", ring: "#fecdd3" },
    { label: "355..424", max: 424, color: "#7e22ce", ring: "#e9d5ff" },
    { label: "> 425", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
  ],
};

const PM1_SCALE: MetricScale = {
  metric: "pm1_0_ug_m3",
  title: "PM1.0",
  hintKey: "cleanToHazardous",
  columns: 3,
  stops: [
    { label: "<= 3", max: 3, color: "#047857", ring: "#a7f3d0" },
    { label: "3..6", max: 6, color: "#16a34a", ring: "#bbf7d0" },
    { label: "6..10", max: 10, color: "#ca8a04", ring: "#fef08a" },
    { label: "10..20", max: 20, color: "#ea580c", ring: "#fed7aa" },
    { label: "20..35", max: 35, color: "#dc2626", ring: "#fecaca" },
    { label: "35..55", max: 55, color: "#7e22ce", ring: "#e9d5ff" },
    { label: "> 55", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
  ],
};

const PM4_SCALE: MetricScale = {
  metric: "pm4_0_ug_m3",
  title: "PM4.0",
  hintKey: "cleanToHazardous",
  columns: 3,
  stops: [
    { label: "<= 8", max: 8, color: "#047857", ring: "#a7f3d0" },
    { label: "8..15", max: 15, color: "#16a34a", ring: "#bbf7d0" },
    { label: "15..25", max: 25, color: "#ca8a04", ring: "#fef08a" },
    { label: "25..50", max: 50, color: "#ea580c", ring: "#fed7aa" },
    { label: "50..100", max: 100, color: "#dc2626", ring: "#fecaca" },
    { label: "100..200", max: 200, color: "#7e22ce", ring: "#e9d5ff" },
    { label: "> 200", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
  ],
};

const DUST_COUNT_SCALE: MetricScale = {
  metric: "dust_concentration_pcs_0_01cf",
  titleKey: "dustCount",
  hintKey: "relativeCount",
  columns: 3,
  stops: [
    { labelKey: "dustTrace", range: "0..1k", max: 1000, color: "#047857", ring: "#a7f3d0" },
    { labelKey: "dustLow", range: "1k..2k", max: 2000, color: "#16a34a", ring: "#bbf7d0" },
    { labelKey: "dustElevated", range: "2k..4k", max: 4000, color: "#ca8a04", ring: "#fef08a" },
    { labelKey: "dustHigh", range: "4k..6k", max: 6000, color: "#ea580c", ring: "#fed7aa" },
    { labelKey: "dustVeryHigh", range: "6k..8k", max: 8000, color: "#be123c", ring: "#fecdd3" },
    { labelKey: "dustOverRange", range: "> 8k", max: Infinity, color: "#7e22ce", ring: "#e9d5ff" },
  ],
  footnote: { unit: "pcs/0.01cf", noteKey: "countEstimate" },
};

const LIGHT_SCALE: MetricScale = {
  metric: "illuminance_lux",
  titleKey: "light",
  hintKey: "darkToBright",
  columns: 2,
  stops: [
    { label: "< 10", max: 10, color: "#1e1b4b", ring: "#c7d2fe" },
    { label: "10..200", max: 200, color: "#2563eb", ring: "#bfdbfe" },
    { label: "200..1000", max: 1000, color: "#0f766e", ring: "#99f6e4" },
    { label: "1000..10000", max: 10000, color: "#ca8a04", ring: "#fef08a" },
    { label: "> 10000", max: Infinity, color: "#ea580c", ring: "#fed7aa" },
  ],
};

export const METRIC_SCALES: Record<MapMetric, MetricScale> = {
  humidity_percent: HUMIDITY_SCALE,
  pressure_hpa: PRESSURE_SCALE,
  temperature_c: TEMPERATURE_SCALE,
  co2_ppm: CO2_SCALE,
  pm10_0_ug_m3: PM10_SCALE,
  pm1_0_ug_m3: PM1_SCALE,
  pm2_5_ug_m3: PM25_SCALE,
  pm4_0_ug_m3: PM4_SCALE,
  dust_concentration_pcs_0_01cf: DUST_COUNT_SCALE,
  illuminance_lux: LIGHT_SCALE,
};

/** Marker colors for one value; `undefined` means the device has no reading. */
export function colorsForValue(
  scale: MetricScale,
  value: number | undefined,
): MarkerColors {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return NO_DATA_COLORS;
  }

  const stop =
    scale.stops.find((candidate) => value <= candidate.max) ??
    scale.stops[scale.stops.length - 1];

  return { color: stop.color, ring: stop.ring };
}

/** CSS gradient that previews the scale's colors in order. */
export function scaleGradient(scale: MetricScale): string {
  return `linear-gradient(90deg, ${scale.stops.map((stop) => stop.color).join(", ")})`;
}

const CLUSTER_AVERAGE = ["/", ["get", "metricSum"], ["get", "metricCount"]];

/**
 * MapLibre `case` expression that colors a cluster by the average of its
 * members' metric values, using the same bands as single markers.
 */
export function clusterColorExpression(
  scale: MetricScale,
  colorKind: keyof MarkerColors,
): unknown[] {
  const expression: unknown[] = [
    "case",
    ["<=", ["get", "metricCount"], 0],
    NO_DATA_COLORS[colorKind],
  ];

  for (const stop of scale.stops.slice(0, -1)) {
    expression.push(["<=", CLUSTER_AVERAGE, stop.max], stop[colorKind]);
  }

  expression.push(scale.stops[scale.stops.length - 1][colorKind]);
  return expression;
}

/** Cluster label: the members' average rounded to one decimal, or empty. */
export function clusterLabelExpression(): unknown[] {
  const roundedAverage = ["/", ["round", ["*", CLUSTER_AVERAGE, 10]], 10];

  return ["case", [">", ["get", "metricCount"], 0], ["to-string", roundedAverage], ""];
}

export function findMetricReading(
  device: DeviceSummary,
  metric: string,
): DeviceReading | undefined {
  for (const sensor of device.sensors) {
    const reading = sensor.readings.find((candidate) => candidate.kind === metric);

    if (reading) {
      return reading;
    }
  }

  return undefined;
}

/** Short marker text; large counts collapse to `1.2k`. */
export function markerValue(metric: MapMetric, value: number): string {
  if (
    (metric === "co2_ppm" || metric === "dust_concentration_pcs_0_01cf") &&
    value >= 1000
  ) {
    return `${formatMarkerNumber(value / 1000)}k`;
  }

  return formatMarkerNumber(value);
}

function formatMarkerNumber(value: number): string {
  return new Intl.NumberFormat(undefined, {
    maximumFractionDigits: 1,
  }).format(value);
}
