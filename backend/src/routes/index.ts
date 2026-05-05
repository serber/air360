import { FastifyPluginAsync } from "fastify";

import { rootRoutes } from "./root";
import { v1Routes } from "./v1";

export const routes: FastifyPluginAsync = async (app) => {
  await app.register(rootRoutes);
  await app.register(v1Routes, { prefix: "/v1" });
};
