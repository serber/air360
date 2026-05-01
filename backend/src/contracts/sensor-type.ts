export const sensorTypes = [
  "bme280",
  "bme680",
  "sps30",
  "scd30",
  "veml7700",
  "gps_nmea",
  "dht11",
  "dht22",
  "htu2x",
  "sht4x",
  "ds18b20",
  "me3_no2",
  "ina219",
  "mhz19b",
  "sds011",
] as const;

export type SensorType = (typeof sensorTypes)[number];

const sensorTypeSet = new Set<string>(sensorTypes);

export function isSensorType(value: string): value is SensorType {
  return sensorTypeSet.has(value);
}
