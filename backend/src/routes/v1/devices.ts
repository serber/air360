import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import {
  findDeviceByDeviceId,
  findDeviceByPublicId,
  upsertDevice,
} from "../../modules/devices/device-repository";
import { findLatestMeasurements } from "../../modules/measurements/measurement-repository";

interface DeviceParams {
  device_id: number;
}

interface DeviceIdParams {
  public_id: string;
}

interface RegisterBody {
  name: string;
  latitude: number;
  longitude: number;
  firmware_version: string;
}

const chipIdParam = {
  schema: {
    params: {
      type: "object",
      properties: { device_id: { type: "integer" } },
      required: ["device_id"],
    },
  },
} as const;

const deviceIdParam = {
  schema: {
    params: {
      type: "object",
      properties: { public_id: { type: "string", format: "uuid" } },
      required: ["public_id"],
    },
  },
} as const;

export const deviceRoutes: FastifyPluginAsync = async (app) => {
  app.put<{ Params: DeviceParams; Body: RegisterBody }>(
    "/devices/:device_id/register",
    chipIdParam,
    async (request, reply) => {
      const { device_id } = request.params;
      const { name, latitude, longitude, firmware_version } =
        request.body ?? {};

      if (!name || typeof name !== "string" || name.trim() === "") {
        return reply.code(400).send({
          error: { code: "validation_error", message: "name is required" },
        });
      }

      if (typeof latitude !== "number" || latitude < -90 || latitude > 90) {
        return reply.code(400).send({
          error: {
            code: "validation_error",
            message: "latitude must be a number between -90 and 90",
          },
        });
      }

      if (typeof longitude !== "number" || longitude < -180 || longitude > 180) {
        return reply.code(400).send({
          error: {
            code: "validation_error",
            message: "longitude must be a number between -180 and 180",
          },
        });
      }

      if (
        !firmware_version ||
        typeof firmware_version !== "string" ||
        firmware_version.trim() === ""
      ) {
        return reply.code(400).send({
          error: {
            code: "validation_error",
            message: "firmware_version is required",
          },
        });
      }

      const db = getDb(app.config);

      const device = await upsertDevice(db, {
        device_id,
        name: name.trim(),
        latitude,
        longitude,
        firmware_version: firmware_version.trim(),
      });

      return reply.code(200).send(device);
    },
  );

  app.get<{ Params: DeviceIdParams }>(
    "/devices/:public_id/latest",
    deviceIdParam,
    async (request, reply) => {
      const { public_id } = request.params;
      const db = getDb(app.config);

      const device = await findDeviceByPublicId(db, public_id);
      if (!device) {
        return reply.code(404).send({
          error: { code: "device_not_found", message: "Device is not registered" },
        });
      }

      const rows = await findLatestMeasurements(db, device.device_id);

      const sensorMap = new Map<string, { kind: string; value: number; sampled_at: Date }[]>();
      for (const row of rows) {
        const existing = sensorMap.get(row.sensor_type) ?? [];
        existing.push({ kind: row.kind, value: row.value, sampled_at: row.sampled_at });
        sensorMap.set(row.sensor_type, existing);
      }

      return reply.code(200).send({
        public_id: device.public_id,
        last_seen_at: device.last_seen_at,
        sensors: Array.from(sensorMap.entries()).map(([sensor_type, readings]) => ({
          sensor_type,
          readings,
        })),
      });
    },
  );
};
