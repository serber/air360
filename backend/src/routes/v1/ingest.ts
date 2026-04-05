import { FastifyPluginAsync } from "fastify";

import {
  isMeasurementKind,
  type MeasurementKind,
} from "../../contracts/measurement-kind";
import { isSensorType, type SensorType } from "../../contracts/sensor-type";

interface IngestParams {
  chip_id: string;
  client_batch_id: string;
}

interface IngestValue {
  kind: MeasurementKind;
  value: number;
}

interface IngestSample {
  sensor_type: SensorType;
  sample_time_unix_ms?: number;
  values: IngestValue[];
}

interface IngestBody {
  schema_version?: number;
  sent_at_unix_ms?: number;
  device?: {
    chip_id?: string;
    device_name?: string;
    board_name?: string;
    short_chip_id?: string;
    esp_mac_id?: string;
    firmware_version?: string;
  };
  batch?: {
    sample_count?: number;
    samples?: IngestSample[];
  };
}

export const ingestRoutes: FastifyPluginAsync = async (app) => {
  app.put<{ Params: IngestParams; Body: IngestBody }>(
    "/devices/:chip_id/batches/:client_batch_id",
    async (request, reply) => {
      const body = request.body;
      const pathChipId = request.params.chip_id;
      const batch = body?.batch;
      const samples = batch?.samples;

      if (body?.device?.chip_id && body.device.chip_id !== pathChipId) {
        return reply.code(400).send({
          accepted: false,
          error: {
            code: "invalid_payload",
            message: "device.chip_id must match the chip_id path parameter",
          },
        });
      }

      if (!Array.isArray(samples)) {
        return reply.code(400).send({
          accepted: false,
          error: {
            code: "invalid_payload",
            message: "batch.samples must be an array",
          },
        });
      }

      if (batch?.sample_count !== samples.length) {
        return reply.code(400).send({
          accepted: false,
          error: {
            code: "invalid_payload",
            message: "batch.sample_count must equal batch.samples.length",
          },
        });
      }

      const missingSampleTime = samples.some(
        (sample) => typeof sample.sample_time_unix_ms !== "number",
      );

      if (missingSampleTime) {
        return reply.code(400).send({
          accepted: false,
          error: {
            code: "invalid_payload",
            message: "sample_time_unix_ms is required for every sample",
          },
        });
      }

      const invalidSensorType = samples.find(
        (sample) => !isSensorType(sample.sensor_type),
      );

      if (invalidSensorType) {
        return reply.code(400).send({
          accepted: false,
          error: {
            code: "invalid_payload",
            message: "every sample.sensor_type must be a supported sensor type",
          },
        });
      }

      const invalidMeasurementKind = samples.find((sample) =>
        sample.values.some((value) => !isMeasurementKind(value.kind)),
      );

      if (invalidMeasurementKind) {
        return reply.code(400).send({
          accepted: false,
          error: {
            code: "invalid_payload",
            message: "every values[].kind must be a supported measurement kind",
          },
        });
      }

      return reply.code(201).send({
        accepted: true,
        client_batch_id: request.params.client_batch_id,
        accepted_samples: samples.length,
      });
    },
  );
};
