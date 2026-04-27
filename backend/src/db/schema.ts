import type { ColumnType, Generated } from "kysely";

export interface DeviceTable {
  id: Generated<string>;
  chip_id: string;
  name: string;
  latitude: number;
  longitude: number;
  firmware_version: string;
  registered_at: ColumnType<Date, never, never>;
  last_seen_at: ColumnType<Date, never, Date>;
}

export interface BatchTable {
  device_id: string;
  batch_id: string;
  received_at: Generated<Date>;
}

export interface MeasurementTable {
  id: Generated<string>;
  device_id: string;
  batch_id: string;
  sensor_type: string;
  kind: string;
  value: number;
  sampled_at: Date;
  received_at: Generated<Date>;
}

export interface Database {
  devices: DeviceTable;
  batches: BatchTable;
  measurements: MeasurementTable;
}

export type Device = {
  id: string;
  chip_id: string;
  name: string;
  latitude: number;
  longitude: number;
  firmware_version: string;
  registered_at: Date;
  last_seen_at: Date;
};
