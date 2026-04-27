import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import {
  findDeviceByChipId,
  updateDeviceLastSeen,
} from "../../modules/devices/device-repository";
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
  chip_id: string;
  batch_id: string;
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
    chip_id?: string;
    firmware_version?: string;
  };
  batch?: {
    sample_count?: number;
    samples?: IngestSample[];
  };
}

export const ingestRoutes: FastifyPluginAsync = async (app) => {
  app.put<{ Params: IngestParams; Body: IngestBody }>(
    "/devices/:chip_id/batches/:batch_id",
    async (request, reply) => {
      const { chip_id, batch_id } = request.params;
      const body = request.body;
      const samples = body?.batch?.samples;

      if (body?.device?.chip_id && body.device.chip_id !== chip_id) {
        return reply.code(400).send({
          error: {
            code: "invalid_payload",
            message: "device.chip_id must match the chip_id path parameter",
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

      const device = await findDeviceByChipId(db, chip_id);
      if (!device) {
        return reply.code(404).send({
          error: {
            code: "device_not_found",
            message: "Device is not registered",
          },
        });
      }

      const inserted = await insertBatch(db, { device_id: device.id, batch_id });

      if (!inserted) {
        // batch already processed — idempotent response
        return reply.code(200).send();
      }

      const measurements = samples.flatMap((sample) =>
        sample.values.map((v) => ({
          device_id: device.id,
          batch_id,
          sensor_type: sample.sensor_type,
          kind: v.kind,
          value: v.value,
          sampled_at: new Date(sample.sample_time_unix_ms),
        })),
      );

      await insertMeasurements(db, measurements);
      await updateDeviceLastSeen(db, device.id);

      return reply.code(200).send();
    },
  );
};
