import { sql, type Kysely } from "kysely";

import type { Database } from "../../db/schema";

export async function enqueueGeoUpdate(
  db: Kysely<Database>,
  device_id: number,
): Promise<void> {
  await db
    .insertInto("geo_update_queue")
    .values({ device_id })
    .onConflict((oc) =>
      oc.column("device_id").doUpdateSet({ queued_at: sql`NOW()` }),
    )
    .execute();
}

export async function dequeueGeoUpdate(
  db: Kysely<Database>,
): Promise<number | null> {
  const row = await sql<{ device_id: number }>`
    DELETE FROM geo_update_queue
    WHERE device_id = (
      SELECT device_id FROM geo_update_queue ORDER BY queued_at LIMIT 1
    )
    RETURNING device_id
  `.execute(db);

  return row.rows[0]?.device_id ?? null;
}
