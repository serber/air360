import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import { findDeviceByPublicId } from "../../modules/devices/device-repository";
import {
  findLatestMeasurements,
  findMeasurementSeries,
} from "../../modules/measurements/measurement-repository";

const PERIOD_CONFIG = {
  "1h":   { bucket: "1 minute",  interval: "1 hour" },
  "24h":  { bucket: "5 minutes", interval: "24 hours" },
  "7d":   { bucket: "1 hour",    interval: "7 days" },
  "30d":  { bucket: "6 hours",   interval: "30 days" },
  "90d":  { bucket: "1 day",     interval: "90 days" },
  "180d": { bucket: "1 day",     interval: "180 days" },
  "365d": { bucket: "1 day",     interval: "365 days" },
} as const;

type Period = keyof typeof PERIOD_CONFIG;

function isPeriod(v: unknown): v is Period {
  return typeof v === "string" && v in PERIOD_CONFIG;
}

interface MeasurementsParams {
  public_id: string;
}

interface MeasurementsQuery {
  period?: string;
}

export const measurementRoutes: FastifyPluginAsync = async (app) => {
  app.get<{ Params: MeasurementsParams; Querystring: MeasurementsQuery }>(
    "/devices/:public_id/measurements",
    {
      schema: {
        params: {
          type: "object",
          properties: { public_id: { type: "string", format: "uuid" } },
          required: ["public_id"],
        },
      },
    },
    async (request, reply) => {
      const { public_id } = request.params;
      const { period } = request.query;

      if (!isPeriod(period)) {
        return reply.code(400).send({
          error: {
            code: "validation_error",
            message: "period must be one of: 1h, 24h, 7d, 30d, 90d, 180d, 365d",
          },
        });
      }

      const db = getDb(app.config);

      const device = await findDeviceByPublicId(db, public_id);
      if (!device) {
        return reply.code(404).send({
          error: { code: "device_not_found", message: "Device is not registered" },
        });
      }

      const { bucket, interval } = PERIOD_CONFIG[period];
      const EXCLUDE_SENSOR_TYPES = ["gps_nmea"];
      const [points, latestReadings] = await Promise.all([
        findMeasurementSeries(db, device.device_id, bucket, interval, EXCLUDE_SENSOR_TYPES),
        findLatestMeasurements(db, device.device_id, EXCLUDE_SENSOR_TYPES),
      ]);

      // Group series by kind → sensor_type so each chart is one measurement type
      const kindMap = new Map<string, Map<string, { t: Date; v: number }[]>>();
      for (const p of points) {
        if (!kindMap.has(p.kind)) kindMap.set(p.kind, new Map());
        const sensorMap = kindMap.get(p.kind)!;
        if (!sensorMap.has(p.sensor_type)) sensorMap.set(p.sensor_type, []);
        sensorMap.get(p.sensor_type)!.push({ t: p.bucket, v: p.v });
      }

      // Build sensor metadata: distinct kinds per sensor_type from latest readings
      const sensorKindsMap = new Map<string, Set<string>>();
      for (const r of latestReadings) {
        if (!sensorKindsMap.has(r.sensor_type)) sensorKindsMap.set(r.sensor_type, new Set());
        sensorKindsMap.get(r.sensor_type)!.add(r.kind);
      }

      return reply.code(200).send({
        public_id,
        period,
        device: {
          name: device.name,
          latitude: device.latitude,
          longitude: device.longitude,
          firmware_version: device.firmware_version,
          registered_at: device.registered_at,
          last_seen_at: device.last_seen_at,
        },
        by_kind: Array.from(kindMap.entries()).map(([kind, sensorMap]) => ({
          kind,
          series: Array.from(sensorMap.entries()).map(([sensor_type, pts]) => ({
            sensor_type,
            points: pts,
          })),
        })),
        latest: latestReadings,
        sensors: Array.from(sensorKindsMap.entries()).map(([sensor_type, kinds]) => ({
          sensor_type,
          kinds: Array.from(kinds),
        })),
      });
    },
  );
};
