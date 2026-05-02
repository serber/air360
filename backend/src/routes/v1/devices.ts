import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import {
  findAllDevices,
  findDeviceByDeviceId,
  findDeviceByPublicId,
  findOfflineDevices,
  upsertDevice,
} from "../../modules/devices/device-repository";
import {
  findAllLatestMeasurements,
  findLatestMeasurements,
} from "../../modules/measurements/measurement-repository";

interface DeviceParams {
  device_id: number;
}

interface DeviceIdParams {
  public_id: string;
}

interface RegisterBody {
  schema_version?: number;
  name: string;
  location: {
    latitude: number;
    longitude: number;
  };
  firmware_version: string;
  upload_secret_hash: string;
}

const deviceRouteParam = {
  schema: {
    params: {
      type: "object",
      properties: { device_id: { type: "integer" } },
      required: ["device_id"],
    },
  },
} as const;

const publicDeviceRouteParam = {
  schema: {
    params: {
      type: "object",
      properties: { public_id: { type: "string", format: "uuid" } },
      required: ["public_id"],
    },
  },
} as const;

export const deviceRoutes: FastifyPluginAsync = async (app) => {
  app.get("/devices", async (_request, reply) => {
    const db = getDb(app.config);

    const [devices, allMeasurements] = await Promise.all([
      findAllDevices(db),
      findAllLatestMeasurements(db),
    ]);

    const measurementsByDevice = new Map<number, typeof allMeasurements>();
    for (const m of allMeasurements) {
      const list = measurementsByDevice.get(m.device_id) ?? [];
      list.push(m);
      measurementsByDevice.set(m.device_id, list);
    }

    return reply.code(200).send({
      devices: devices.map((device) => {
        const measurements = measurementsByDevice.get(device.device_id) ?? [];
        const sensorMap = new Map<string, { kind: string; value: number; sampled_at: Date }[]>();
        for (const m of measurements) {
          const readings = sensorMap.get(m.sensor_type) ?? [];
          readings.push({ kind: m.kind, value: m.value, sampled_at: m.sampled_at });
          sensorMap.set(m.sensor_type, readings);
        }
        return {
          public_id: device.public_id,
          name: device.name,
          location: { latitude: device.latitude, longitude: device.longitude },
          last_seen_at: device.last_seen_at,
          sensors: Array.from(sensorMap.entries()).map(([sensor_type, readings]) => ({
            sensor_type,
            readings,
          })),
        };
      }),
    });
  });

  app.get("/devices/offline", async (_request, reply) => {
    const db = getDb(app.config);
    const devices = await findOfflineDevices(db);

    return reply.code(200).send({
      devices: devices.map((device) => ({
        public_id: device.public_id,
        name: device.name,
        location: { latitude: device.latitude, longitude: device.longitude },
        last_seen_at: device.last_seen_at,
        sensors: [],
      })),
    });
  });

  app.put<{ Params: DeviceParams; Body: RegisterBody }>(
    "/devices/:device_id/register",
    deviceRouteParam,
    async (request, reply) => {
      const { device_id } = request.params;
      const { name, location, firmware_version, upload_secret_hash } = request.body ?? {};

      if (!name || typeof name !== "string" || name.trim() === "") {
        return reply.code(400).send({
          error: { code: "validation_error", message: "name is required" },
        });
      }

      if (!location || typeof location !== "object") {
        return reply.code(400).send({
          error: { code: "validation_error", message: "location is required" },
        });
      }

      const { latitude, longitude } = location;

      if (typeof latitude !== "number" || latitude < -90 || latitude > 90) {
        return reply.code(400).send({
          error: {
            code: "validation_error",
            message: "location.latitude must be a number between -90 and 90",
          },
        });
      }

      if (typeof longitude !== "number" || longitude < -180 || longitude > 180) {
        return reply.code(400).send({
          error: {
            code: "validation_error",
            message: "location.longitude must be a number between -180 and 180",
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

      if (
        !upload_secret_hash ||
        typeof upload_secret_hash !== "string" ||
        !upload_secret_hash.startsWith("sha256:")
      ) {
        return reply.code(400).send({
          error: {
            code: "validation_error",
            message: "upload_secret_hash is required and must begin with 'sha256:'",
          },
        });
      }

      const db = getDb(app.config);

      const existing = await findDeviceByDeviceId(db, device_id);
      if (existing?.upload_secret_hash && existing.upload_secret_hash !== upload_secret_hash) {
        return reply.code(401).send({
          error: {
            code: "invalid_upload_secret",
            message: "Upload secret does not match this device",
          },
        });
      }

      const device = await upsertDevice(db, {
        device_id,
        registered_from_ip: request.ip ?? null,
        name: name.trim(),
        latitude,
        longitude,
        firmware_version: firmware_version.trim(),
        upload_secret_hash,
      });

      return reply.code(200).send({
        schema_version: 1,
        status: "registered",
        public_id: device.public_id,
        registered_at: device.registered_at,
        last_seen_at: device.last_seen_at,
      });
    },
  );

  app.get<{ Params: DeviceIdParams }>(
    "/devices/:public_id/latest",
    publicDeviceRouteParam,
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
