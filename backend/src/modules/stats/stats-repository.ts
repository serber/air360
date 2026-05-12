import { sql, type Kysely } from "kysely";

import type { Database } from "../../db/schema";

export interface PortalStats {
  active_devices: number;
  countries: number;
  data_points_24h: number;
}

export async function getPortalStats(
  db: Kysely<Database>,
): Promise<PortalStats> {
  const result = await sql<PortalStats>`
    SELECT
      (
        SELECT COUNT(*)
        FROM devices
        WHERE last_seen_at >= NOW() - INTERVAL '1 hour'
      ) AS active_devices,
      (
        SELECT COUNT(DISTINCT geo_country_code)
        FROM devices
        WHERE geo_country_code IS NOT NULL AND geo_country_code <> ''
      ) AS countries,
      (
        SELECT COUNT(*)
        FROM measurements
        WHERE sampled_at >= NOW() - INTERVAL '24 hours'
      ) AS data_points_24h
  `.execute(db);

  return result.rows[0] ?? {
    active_devices: 0,
    countries: 0,
    data_points_24h: 0,
  };
}
