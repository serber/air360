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

export interface Database {
  devices: DeviceTable;
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
