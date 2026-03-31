import { FastifyPluginAsync } from "fastify";

import { ingestRoutes } from "./ingest";

export const v1Routes: FastifyPluginAsync = async (app) => {
  await app.register(ingestRoutes);
};
