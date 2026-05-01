export type Period = "1h" | "24h" | "7d" | "30d" | "90d" | "180d" | "365d";

export type DeviceReading = {
  kind: string;
  value: number;
  sampled_at: string;
};

export type DeviceSensor = {
  sensor_type: string;
  readings: DeviceReading[];
};

export type DeviceSummary = {
  public_id: string;
  name: string;
  location: {
    latitude: number;
    longitude: number;
  };
  last_seen_at: string;
  sensors: DeviceSensor[];
};

export type DevicesResponse = {
  devices: DeviceSummary[];
};

export type MeasurementPoint = {
  t: string;
  v: number;
};

export type MeasurementSeries = {
  kind: string;
  points: MeasurementPoint[];
};

export type SensorMeasurements = {
  sensor_type: string;
  series: MeasurementSeries[];
};

export type MeasurementsResponse = {
  public_id: string;
  period: Period;
  sensors: SensorMeasurements[];
};

export const PERIOD_OPTIONS: Array<{ label: string; value: Period }> = [
  { label: "1 hour", value: "1h" },
  { label: "24 hours", value: "24h" },
  { label: "7 days", value: "7d" },
  { label: "30 days", value: "30d" },
  { label: "3 months", value: "90d" },
  { label: "6 months", value: "180d" },
  { label: "1 year", value: "365d" },
];

const API_BASE_URL = process.env.NEXT_PUBLIC_AIR360_API_BASE_URL?.replace(
  /\/+$/,
  "",
);

export function apiUrl(path: string): string {
  const normalizedPath = path.startsWith("/") ? path : `/${path}`;
  return API_BASE_URL ? `${API_BASE_URL}${normalizedPath}` : normalizedPath;
}

export async function fetchJson<T>(
  path: string,
  signal?: AbortSignal,
): Promise<T> {
  const response = await fetch(apiUrl(path), {
    headers: { Accept: "application/json" },
    signal,
  });

  if (!response.ok) {
    throw new Error(`Request failed with HTTP ${response.status}`);
  }

  return (await response.json()) as T;
}

export function formatDateTime(value: string): string {
  const date = new Date(value);

  if (Number.isNaN(date.getTime())) {
    return value;
  }

  return new Intl.DateTimeFormat(undefined, {
    dateStyle: "medium",
    timeStyle: "short",
  }).format(date);
}

export function formatChartTime(value: string): string {
  const date = new Date(value);

  if (Number.isNaN(date.getTime())) {
    return value;
  }

  return new Intl.DateTimeFormat(undefined, {
    month: "short",
    day: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  }).format(date);
}

export function formatValue(kind: string, value: number): string {
  const unit = valueUnit(kind);

  return `${new Intl.NumberFormat(undefined, {
    maximumFractionDigits: Math.abs(value) >= 100 ? 0 : 2,
  }).format(value)}${unit ? ` ${unit}` : ""}`;
}

export function sensorLabel(sensorType: string): string {
  return sensorType
    .split(/[_-]+/)
    .filter(Boolean)
    .map((part) => part.toUpperCase())
    .join(" ");
}

export function kindLabel(kind: string): string {
  const labels: Record<string, string> = {
    altitude_m: "Altitude",
    adc_raw: "Raw ADC",
    bus_voltage_v: "Bus voltage",
    co2_ppm: "CO2",
    course_deg: "Course",
    current_ma: "Current",
    gas_resistance_ohms: "Gas resistance",
    hdop: "HDOP",
    humidity_percent: "Humidity",
    illuminance_lux: "Illuminance",
    latitude_deg: "Latitude",
    longitude_deg: "Longitude",
    nc0_5_per_cm3: "NC0.5",
    nc1_0_per_cm3: "NC1.0",
    nc2_5_per_cm3: "NC2.5",
    nc4_0_per_cm3: "NC4.0",
    nc10_0_per_cm3: "NC10.0",
    no2_voltage_v: "NO2 voltage",
    particle_size_um: "Particle size",
    pm1_0_ug_m3: "PM1.0",
    pm2_5_ug_m3: "PM2.5",
    pm4_0_ug_m3: "PM4.0",
    pm10_0_ug_m3: "PM10",
    pm10_ug_m3: "PM10",
    power_mw: "Power",
    pressure_hpa: "Pressure",
    raw_adc: "Raw ADC",
    satellites: "Satellites",
    speed_knots: "Speed",
    speed_kmh: "Speed",
    temperature_c: "Temperature",
    typical_particle_size_um: "Typical particle size",
    voltage_mv: "Voltage",
  };

  return (
    labels[kind] ??
    kind
      .split(/[_-]+/)
      .filter(Boolean)
      .map((part) => part[0]?.toUpperCase() + part.slice(1))
      .join(" ")
  );
}

function valueUnit(kind: string): string {
  if (kind.endsWith("_c")) return "C";
  if (kind.endsWith("_percent")) return "%";
  if (kind.endsWith("_hpa")) return "hPa";
  if (kind.endsWith("_ppm")) return "ppm";
  if (kind.endsWith("_ug_m3")) return "ug/m3";
  if (kind.endsWith("_lux")) return "lx";
  if (kind.endsWith("_m")) return "m";
  if (kind.endsWith("_mv")) return "mV";
  if (kind.endsWith("_v")) return "V";
  if (kind.endsWith("_ma")) return "mA";
  if (kind.endsWith("_mw")) return "mW";
  if (kind.endsWith("_kmh")) return "km/h";
  if (kind.endsWith("_knots")) return "kn";
  if (kind.endsWith("_deg")) return "deg";
  if (kind.endsWith("_um")) return "um";
  if (kind.endsWith("_ohms")) return "ohm";
  if (kind.endsWith("_ohm")) return "ohm";
  if (kind.endsWith("_per_cm3")) return "/cm3";

  return "";
}
