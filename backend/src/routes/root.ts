import { FastifyPluginAsync } from "fastify";

export const rootRoutes: FastifyPluginAsync = async (app) => {
  app.get("/", async () => ({
    service: "air360-api-backend",
    status: "ok",
  }));
};
