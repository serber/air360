import { sql, type Kysely } from "kysely";

import type { Database } from "../../db/schema";

export interface LatestReading {
  sensor_type: string;
  kind: string;
  value: number;
  sampled_at: Date;
}

export async function findLatestMeasurements(
  db: Kysely<Database>,
  device_id: number,
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

export interface LatestReadingWithDevice extends LatestReading {
  device_id: number;
}

export async function findAllLatestMeasurements(
  db: Kysely<Database>,
): Promise<LatestReadingWithDevice[]> {
  const result = await sql<LatestReadingWithDevice>`
    SELECT DISTINCT ON (m.device_id, m.sensor_type, m.kind)
      m.device_id, m.sensor_type, m.kind, m.value, m.sampled_at
    FROM measurements m
    JOIN devices d ON m.device_id = d.device_id AND m.batch_id = d.last_batch_id
    WHERE d.last_batch_id IS NOT NULL
    ORDER BY m.device_id, m.sensor_type, m.kind, m.sampled_at DESC
  `.execute(db);
  return result.rows;
}

export interface MeasurementPoint {
  sensor_type: string;
  kind: string;
  bucket: Date;
  v: number;
}

export async function findMeasurementSeries(
  db: Kysely<Database>,
  device_id: number,
  bucketSize: string,
  intervalSize: string,
): Promise<MeasurementPoint[]> {
  // bucketSize and intervalSize come from a server-side whitelist — sql.raw is safe here
  const result = await sql<MeasurementPoint>`
    SELECT
      sensor_type,
      kind,
      time_bucket(${sql.raw("'" + bucketSize + "'")}, sampled_at) AS bucket,
      AVG(value)::float AS v
    FROM measurements
    WHERE device_id = ${device_id}
      AND sampled_at >= NOW() - ${sql.raw("'" + intervalSize + "'")}::interval
    GROUP BY sensor_type, kind, bucket
    ORDER BY sensor_type, kind, bucket
  `.execute(db);
  return result.rows;
}
