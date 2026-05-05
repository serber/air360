import { sql, type Kysely } from "kysely";

import type { Database, Device } from "../../db/schema";

interface UpsertDeviceData {
  device_id: number;
  registered_from_ip: string | null;
  name: string;
  latitude: number;
  longitude: number;
  altitude_m: number | null;
  firmware_version: string;
  upload_secret_hash: string | null;
}

export async function upsertDevice(
  db: Kysely<Database>,
  data: UpsertDeviceData,
): Promise<Device> {
  return db
    .insertInto("devices")
    .values(data)
    .onConflict((oc) =>
      oc.column("device_id").doUpdateSet((eb) => ({
        name: data.name,
        latitude: data.latitude,
        longitude: data.longitude,
        altitude_m: data.altitude_m,
        firmware_version: data.firmware_version,
        last_seen_at: new Date(),
        // TOFU: keep existing hash if already set, otherwise store the new one
        upload_secret_hash: sql`COALESCE(${eb.ref("devices.upload_secret_hash")}, ${data.upload_secret_hash})`,
      })),
    )
    .returningAll()
    .executeTakeFirstOrThrow() as Promise<Device>;
}

export async function findAllDevices(db: Kysely<Database>): Promise<Device[]> {
  return db
    .selectFrom("devices")
    .selectAll()
    .where("last_seen_at", ">=", sql<Date>`NOW() - INTERVAL '1 hour'`)
    .execute() as Promise<Device[]>;
}

export async function findOfflineDevices(db: Kysely<Database>): Promise<Device[]> {
  return db
    .selectFrom("devices")
    .selectAll()
    .where("last_seen_at", "<", sql<Date>`NOW() - INTERVAL '1 hour'`)
    .execute() as Promise<Device[]>;
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

interface UpdateDeviceOnIngestData {
  batch_id: number;
  location?: {
    latitude: number;
    longitude: number;
    altitude_m: number | null;
  };
}

export async function updateDeviceOnIngest(
  db: Kysely<Database>,
  device_id: number,
  data: UpdateDeviceOnIngestData,
): Promise<void> {
  await db
    .updateTable("devices")
    .set({
      last_seen_at: new Date(),
      last_batch_id: data.batch_id,
      ...(data.location ?? {}),
    })
    .where("device_id", "=", device_id)
    .execute();
}
