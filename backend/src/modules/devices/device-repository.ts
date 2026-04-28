import type { Kysely } from "kysely";

import type { Database, Device } from "../../db/schema";

interface UpsertDeviceData {
  device_id: number;
  registered_from_ip: string | null;
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
      oc.column("device_id").doUpdateSet({
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

export async function findDeviceByDeviceId(
  db: Kysely<Database>,
  device_id: number,
): Promise<Device | undefined> {
  return db
    .selectFrom("devices")
    .selectAll()
    .where("device_id", "=", device_id)
    .executeTakeFirst() as Promise<Device | undefined>;
}

export async function findDeviceByPublicId(
  db: Kysely<Database>,
  public_id: string,
): Promise<Device | undefined> {
  return db
    .selectFrom("devices")
    .selectAll()
    .where("public_id", "=", public_id)
    .executeTakeFirst() as Promise<Device | undefined>;
}

export async function updateDeviceLastSeen(
  db: Kysely<Database>,
  device_id: number,
): Promise<void> {
  await db
    .updateTable("devices")
    .set({ last_seen_at: new Date() })
    .where("device_id", "=", device_id)
    .execute();
}
