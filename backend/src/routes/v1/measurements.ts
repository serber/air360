import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import { findDeviceByPublicId } from "../../modules/devices/device-repository";
import { findMeasurementSeries } from "../../modules/measurements/measurement-repository";

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
      const points = await findMeasurementSeries(db, device.device_id, bucket, interval);

      const sensorMap = new Map<string, Map<string, { t: Date; v: number }[]>>();
      for (const p of points) {
        if (!sensorMap.has(p.sensor_type)) sensorMap.set(p.sensor_type, new Map());
        const kindMap = sensorMap.get(p.sensor_type)!;
        if (!kindMap.has(p.kind)) kindMap.set(p.kind, []);
        kindMap.get(p.kind)!.push({ t: p.bucket, v: p.v });
      }

      return reply.code(200).send({
        public_id,
        period,
        sensors: Array.from(sensorMap.entries()).map(([sensor_type, kindMap]) => ({
          sensor_type,
          series: Array.from(kindMap.entries()).map(([kind, pts]) => ({ kind, points: pts })),
        })),
      });
    },
  );
};
