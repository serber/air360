import type { Kysely } from "kysely";

import type { Database, Device } from "../../db/schema";

interface UpsertDeviceData {
  chip_id: string;
  name: string;
  latitude: number;
  longitude: number;
  firmware_version: string;
}

export async function upsertDevice(
  db: Kysely<Database>,
  data: UpsertDeviceData,
): Promise<Device> {
  return db
    .insertInto("devices")
    .values(data)
    .onConflict((oc) =>
      oc.column("chip_id").doUpdateSet({
        name: data.name,
        latitude: data.latitude,
        longitude: data.longitude,
        firmware_version: data.firmware_version,
        last_seen_at: new Date(),
      }),
    )
    .returningAll()
    .executeTakeFirstOrThrow() as Promise<Device>;
}

export async function findDeviceByChipId(
  db: Kysely<Database>,
  chip_id: string,
): Promise<Device | undefined> {
  return db
    .selectFrom("devices")
    .selectAll()
    .where("chip_id", "=", chip_id)
    .executeTakeFirst() as Promise<Device | undefined>;
}

export async function updateDeviceLastSeen(
  db: Kysely<Database>,
  device_id: string,
): Promise<void> {
  await db
    .updateTable("devices")
    .set({ last_seen_at: new Date() })
    .where("id", "=", device_id)
    .execute();
}
