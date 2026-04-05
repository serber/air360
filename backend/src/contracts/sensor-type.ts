export const sensorTypes = [
  "bme280",
  "gps_nmea",
  "dht11",
  "dht22",
  "bme680",
  "sps30",
  "ens160",
  "me3_no2",
] as const;

export type SensorType = (typeof sensorTypes)[number];

const sensorTypeSet = new Set<string>(sensorTypes);

export function isSensorType(value: string): value is SensorType {
  return sensorTypeSet.has(value);
}
