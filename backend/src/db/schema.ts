import type { ColumnType, Generated } from "kysely";

export interface DeviceTable {
  device_id: number;
  public_id: Generated<string>;
  registered_from_ip: ColumnType<string | null, string | null, never>;
  name: string;
  latitude: number;
  longitude: number;
  altitude_m: ColumnType<number | null, number | null, number | null>;
  firmware_version: string;
  upload_secret_hash: ColumnType<string | null, string | null, string | null>;
  last_batch_id: ColumnType<number | null, number | null, number | null>;
  geo_country: ColumnType<string | null, string | null, string | null>;
  geo_country_code: ColumnType<string | null, string | null, string | null>;
  geo_city: ColumnType<string | null, string | null, string | null>;
  geo_display: ColumnType<string | null, string | null, string | null>;
  registered_at: ColumnType<Date, never, never>;
  last_seen_at: ColumnType<Date, never, Date>;
}

export interface GeoUpdateQueueTable {
  device_id: number;
  queued_at: Generated<Date>;
}

export interface BatchTable {
  device_id: number;
  batch_id: number;
  received_at: Generated<Date>;
}

export interface MeasurementTable {
  device_id: number;
  batch_id: number;
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
  geo_update_queue: GeoUpdateQueueTable;
}

export type Device = {
  device_id: number;
  public_id: string;
  registered_from_ip: string | null;
  name: string;
  latitude: number;
  longitude: number;
  altitude_m: number | null;
  firmware_version: string;
  upload_secret_hash: string | null;
  last_batch_id: number | null;
  geo_country: string | null;
  geo_country_code: string | null;
  geo_city: string | null;
  geo_display: string | null;
  registered_at: Date;
  last_seen_at: Date;
};
