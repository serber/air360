import type { FastifyPluginAsync } from "fastify";

import { getDb } from "../../db/client";
import { getPortalStats } from "../../modules/stats/stats-repository";

export const statsRoutes: FastifyPluginAsync = async (app) => {
  app.get("/stats", async (_request, reply) => {
    const db = getDb(app.config);
    const stats = await getPortalStats(db);

    return reply.code(200).send(stats);
  });
};
