import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import { GEO_UPDATE_THRESHOLD_METERS, haversineDistanceMeters } from "../../lib/geo";
import { toSeaLevelPressure } from "../../lib/pressure";
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
  measurementKinds,
  type MeasurementKind,
  type StoredMeasurementKind,
} from "../../contracts/measurement-kind";
import { sensorTypes, type SensorType } from "../../contracts/sensor-type";

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
  batch: {
    sample_count: number;
    samples: IngestSample[];
  };
}

// Firmware only uploads once it has valid Unix time, so anything outside this
// window is a corrupted timestamp rather than a legitimate sample.
const MIN_SAMPLE_TIME_UNIX_MS = Date.UTC(2020, 0, 1);
const MAX_SAMPLE_TIME_UNIX_MS = Date.UTC(2100, 0, 1);
const MAX_SAMPLES_PER_BATCH = 2000;
const MAX_VALUES_PER_SAMPLE = 64;

const ingestBodySchema = {
  type: "object",
  required: ["batch"],
  properties: {
    schema_version: { type: "integer" },
    sent_at_unix_ms: { type: "number" },
    device: {
      type: "object",
      properties: {
        device_id: { type: "string" },
        firmware_version: { type: "string" },
      },
    },
    batch: {
      type: "object",
      required: ["sample_count", "samples"],
      properties: {
        sample_count: { type: "integer", minimum: 0 },
        samples: {
          type: "array",
          maxItems: MAX_SAMPLES_PER_BATCH,
          items: {
            type: "object",
            required: ["sensor_type", "sample_time_unix_ms", "values"],
            properties: {
              sensor_type: { type: "string", enum: sensorTypes },
              sample_time_unix_ms: {
                type: "integer",
                minimum: MIN_SAMPLE_TIME_UNIX_MS,
                maximum: MAX_SAMPLE_TIME_UNIX_MS,
              },
              values: {
                type: "array",
                maxItems: MAX_VALUES_PER_SAMPLE,
                items: {
                  type: "object",
                  required: ["kind", "value"],
                  properties: {
                    kind: { type: "string", enum: measurementKinds },
                    value: { type: "number" },
                  },
                },
              },
            },
          },
        },
      },
    },
  },
} as const;

interface StoredMeasurement {
  device_id: number;
  batch_id: number;
  sensor_type: SensorType;
  kind: StoredMeasurementKind;
  value: number;
  sampled_at: Date;
}

export const ingestRoutes: FastifyPluginAsync = async (app) => {
  app.put<{ Params: IngestParams; Body: IngestBody }>(
    "/devices/:device_id/batches/:batch_id",
    {
      // Schema failures are reported under the documented `invalid_payload`
      // code instead of the generic validation_error from the error handler.
      attachValidation: true,
      schema: {
        params: {
          type: "object",
          properties: {
            device_id: { type: "integer" },
            batch_id: { type: "integer" },
          },
          required: ["device_id", "batch_id"],
        },
        body: ingestBodySchema,
      },
    },
    async (request, reply) => {
      if (request.validationError) {
        return reply.code(400).send({
          error: { code: "invalid_payload", message: request.validationError.message },
        });
      }

      const { device_id, batch_id } = request.params;
      const body = request.body;
      const samples = body.batch.samples;

      if (body.device?.device_id && Number(body.device.device_id) !== device_id) {
        return reply.code(400).send({
          error: {
            code: "invalid_payload",
            message: "device.device_id must match the device_id path parameter",
          },
        });
      }

      if (body.batch.sample_count !== samples.length) {
        return reply.code(400).send({
          error: {
            code: "invalid_payload",
            message: "batch.sample_count must equal batch.samples.length",
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

      const altitude_m = device.altitude_m;
      const measurements: StoredMeasurement[] = samples.flatMap((sample) =>
        sample.values.flatMap((v): StoredMeasurement[] => {
          const base: StoredMeasurement = {
            device_id,
            batch_id,
            sensor_type: sample.sensor_type,
            kind: v.kind,
            value: v.value,
            sampled_at: new Date(sample.sample_time_unix_ms),
          };

          if (v.kind === "pressure_hpa" && altitude_m !== null && altitude_m !== 0) {
            return [
              { ...base, kind: "pressure_hpa_raw" },
              { ...base, value: toSeaLevelPressure(v.value, altitude_m) },
            ];
          }

          return [base];
        }),
      );

      const latestGps = samples
        .filter((s) => s.sensor_type === "gps_nmea")
        .sort((a, b) => b.sample_time_unix_ms - a.sample_time_unix_ms)[0];

      let location: { latitude: number; longitude: number; altitude_m: number | null } | undefined;
      if (latestGps !== undefined) {
        const valueOf = (kind: MeasurementKind) =>
          latestGps.values.find((v) => v.kind === kind)?.value ?? null;
        const latitude = valueOf("latitude_deg");
        const longitude = valueOf("longitude_deg");
        if (latitude !== null && longitude !== null) {
          location = { latitude, longitude, altitude_m: valueOf("altitude_m") };
        }
      }

      const needsGeoRefresh =
        location !== undefined &&
        (device.geo_display === null ||
          haversineDistanceMeters(
            device.latitude, device.longitude,
            location.latitude, location.longitude,
          ) > GEO_UPDATE_THRESHOLD_METERS);

      // The batch marker, its measurements, and the device bookkeeping commit
      // together: a half-written batch would otherwise be acknowledged as a
      // duplicate on retry and its measurements lost for good.
      await db.transaction().execute(async (trx) => {
        const inserted = await insertBatch(trx, { device_id, batch_id });
        if (!inserted) {
          // batch already processed — idempotent response
          return;
        }

        await insertMeasurements(trx, measurements);
        await updateDeviceOnIngest(trx, device_id, {
          batch_id,
          ...(location ? { location } : {}),
        });

        if (needsGeoRefresh) {
          await enqueueGeoUpdate(trx, device_id);
        }
      });

      return reply.code(200).send();
    },
  );
};
