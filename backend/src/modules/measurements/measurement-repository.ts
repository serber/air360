import type { Kysely } from "kysely";

import type { Database } from "../../db/schema";

export interface LatestReading {
  sensor_type: string;
  kind: string;
  value: number;
  sampled_at: Date;
}

export async function findLatestMeasurements(
  db: Kysely<Database>,
  device_id: string,
): Promise<LatestReading[]> {
  return db
    .selectFrom("measurements")
    .select(["sensor_type", "kind", "value", "sampled_at"])
    .distinctOn(["sensor_type", "kind"])
    .where("device_id", "=", device_id)
    .orderBy("sensor_type")
    .orderBy("kind")
    .orderBy("sampled_at", "desc")
    .execute() as Promise<LatestReading[]>;
}
