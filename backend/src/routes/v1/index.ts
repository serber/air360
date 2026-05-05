import { FastifyPluginAsync } from "fastify";

import { deviceRoutes } from "./devices";
import { ingestRoutes } from "./ingest";
import { measurementRoutes } from "./measurements";

export const v1Routes: FastifyPluginAsync = async (app) => {
  await app.register(deviceRoutes);
  await app.register(ingestRoutes);
  await app.register(measurementRoutes);
};
