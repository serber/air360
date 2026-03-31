export const measurementKinds = [
  "temperature_c",
  "humidity_percent",
  "pressure_hpa",
  "latitude_deg",
  "longitude_deg",
  "altitude_m",
  "satellites",
  "speed_knots",
  "gas_resistance_ohms",
  "pm1_0_ug_m3",
  "pm2_5_ug_m3",
  "pm4_0_ug_m3",
  "pm10_0_ug_m3",
  "nc0_5_per_cm3",
  "nc1_0_per_cm3",
  "nc2_5_per_cm3",
  "nc4_0_per_cm3",
  "nc10_0_per_cm3",
  "typical_particle_size_um",
  "aqi",
  "tvoc_ppb",
  "eco2_ppm",
  "adc_raw",
  "voltage_mv",
] as const;

export type MeasurementKind = (typeof measurementKinds)[number];

const measurementKindSet = new Set<string>(measurementKinds);

export function isMeasurementKind(value: string): value is MeasurementKind {
  return measurementKindSet.has(value);
}
