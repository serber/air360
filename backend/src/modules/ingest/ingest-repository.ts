import type { Kysely } from "kysely";

import type { Database } from "../../db/schema";

interface InsertBatchData {
  device_id: string;
  batch_id: string;
}

interface InsertMeasurementData {
  device_id: string;
  batch_id: string;
  sensor_type: string;
  kind: string;
  value: number;
  sampled_at: Date;
}

export async function insertBatch(
  db: Kysely<Database>,
  data: InsertBatchData,
): Promise<boolean> {
  const row = await db
    .insertInto("batches")
    .values(data)
    .onConflict((oc) =>
      oc.columns(["device_id", "batch_id"]).doNothing(),
    )
    .returning("batch_id")
    .executeTakeFirst();

  return row !== undefined;
}

export async function insertMeasurements(
  db: Kysely<Database>,
  measurements: InsertMeasurementData[],
): Promise<void> {
  if (measurements.length === 0) return;

  await db.insertInto("measurements").values(measurements).execute();
}
