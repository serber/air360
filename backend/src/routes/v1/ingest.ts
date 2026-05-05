import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import { GEO_UPDATE_THRESHOLD_METERS, haversineDistanceMeters } from "../../lib/geo";
import { verifyUploadSecret } from "../../lib/upload-secret";
import {
  findDeviceByDeviceId,
  updateDeviceOnIngest,
} from "../../modules/devices/device-repository";
import { enqueueGeoUpdate } from "../../modules/geo/geo-queue-repository";
import {
  insertBatch,
  insertMeasurements,
} from "../../modules/ingest/ingest-repository";
import {
  isMeasurementKind,
  type MeasurementKind,
} from "../../contracts/measurement-kind";
import { isSensorType, type SensorType } from "../../contracts/sensor-type";

interface IngestParams {
  device_id: number;
  batch_id: number;
}

interface IngestValue {
  kind: MeasurementKind;
  value: number;
}

interface IngestSample {
  sensor_type: SensorType;
  sample_time_unix_ms: number;
  values: IngestValue[];
}

interface IngestBody {
  schema_version?: number;
  sent_at_unix_ms?: number;
  device?: {
    device_id?: string;
    firmware_version?: string;
  };
  batch?: {
    sample_count?: number;
    samples?: IngestSample[];
  };
}

export const ingestRoutes: FastifyPluginAsync = async (app) => {
  app.put<{ Params: IngestParams; Body: IngestBody }>(
    "/devices/:device_id/batches/:batch_id",
    {
      schema: {
        params: {
          type: "object",
          properties: {
            device_id: { type: "integer" },
            batch_id: { type: "integer" },
          },
          required: ["device_id", "batch_id"],
        },
      },
    },
    async (request, reply) => {
      const { device_id, batch_id } = request.params;
      const body = request.body;
      const samples = body?.batch?.samples;

      if (body?.device?.device_id && Number(body.device.device_id) !== device_id) {
        return reply.code(400).send({
          error: {
            code: "invalid_payload",
            message: "device.device_id must match the device_id path parameter",
          },
        });
      }

      if (!Array.isArray(samples)) {
        return reply.code(400).send({
          error: { code: "invalid_payload", message: "batch.samples must be an array" },
        });
      }

      if (body?.batch?.sample_count !== samples.length) {
        return reply.code(400).send({
          error: {
            code: "invalid_payload",
            message: "batch.sample_count must equal batch.samples.length",
          },
        });
      }

      const missingSampleTime = samples.some(
        (s) => typeof s.sample_time_unix_ms !== "number",
      );
      if (missingSampleTime) {
        return reply.code(400).send({
          error: {
            code: "invalid_payload",
            message: "sample_time_unix_ms is required for every sample",
          },
        });
      }

      const invalidSensorType = samples.find((s) => !isSensorType(s.sensor_type));
      if (invalidSensorType) {
        return reply.code(400).send({
          error: {
            code: "invalid_payload",
            message: "every sample.sensor_type must be a supported sensor type",
          },
        });
      }

      const invalidKind = samples.find((s) =>
        s.values.some((v) => !isMeasurementKind(v.kind)),
      );
      if (invalidKind) {
        return reply.code(400).send({
          error: {
            code: "invalid_payload",
            message: "every values[].kind must be a supported measurement kind",
          },
        });
      }

      const db = getDb(app.config);

      const device = await findDeviceByDeviceId(db, device_id);
      if (!device) {
        return reply.code(404).send({
          error: {
            code: "device_not_found",
            message: "Device is not registered",
          },
        });
      }

      const authHeader = request.headers["authorization"];
      const bearerPrefix = "Bearer ";
      const bearerSecret =
        authHeader?.startsWith(bearerPrefix)
          ? authHeader.slice(bearerPrefix.length)
          : null;

      if (
        !bearerSecret ||
        !device.upload_secret_hash ||
        !verifyUploadSecret(bearerSecret, device.upload_secret_hash)
      ) {
        return reply.code(401).send({
          error: {
            code: "invalid_upload_secret",
            message: "Upload secret does not match this device",
          },
        });
      }

      const inserted = await insertBatch(db, { device_id, batch_id });

      if (!inserted) {
        // batch already processed — idempotent response
        return reply.code(200).send();
      }

      const measurements = samples.flatMap((sample) =>
        sample.values.map((v) => ({
          device_id,
          batch_id,
          sensor_type: sample.sensor_type,
          kind: v.kind,
          value: v.value,
          sampled_at: new Date(sample.sample_time_unix_ms),
        })),
      );

      await insertMeasurements(db, measurements);

      const latestGps = samples
        .filter((s) => s.sensor_type === "gps_nmea")
        .sort((a, b) => b.sample_time_unix_ms - a.sample_time_unix_ms)[0];

      let location: { latitude: number; longitude: number; altitude_m: number | null } | undefined;
      if (latestGps !== undefined) {
        const valueOf = (kind: string) =>
          latestGps.values.find((v) => v.kind === kind)?.value ?? null;
        const latitude = valueOf("latitude_deg");
        const longitude = valueOf("longitude_deg");
        if (latitude !== null && longitude !== null) {
          location = { latitude, longitude, altitude_m: valueOf("altitude_m") };
        }
      }

      await updateDeviceOnIngest(db, device_id, { batch_id, ...(location ? { location } : {}) });

      if (location !== undefined) {
        const distance = haversineDistanceMeters(
          device.latitude, device.longitude,
          location.latitude, location.longitude,
        );
        if (distance > GEO_UPDATE_THRESHOLD_METERS || device.geo_display === null) {
          await enqueueGeoUpdate(db, device_id);
        }
      }

      return reply.code(200).send();
    },
  );
};
