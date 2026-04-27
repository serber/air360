import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import { upsertDevice } from "../../modules/devices/device-repository";

interface RegisterParams {
  chip_id: string;
}

interface RegisterBody {
  name: string;
  latitude: number;
  longitude: number;
  firmware_version: string;
}

export const deviceRoutes: FastifyPluginAsync = async (app) => {
  app.put<{ Params: RegisterParams; Body: RegisterBody }>(
    "/devices/:chip_id/register",
    async (request, reply) => {
      const { chip_id } = request.params;
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
        chip_id,
        name: name.trim(),
        latitude,
        longitude,
        firmware_version: firmware_version.trim(),
      });

      return reply.code(200).send(device);
    },
  );
};
