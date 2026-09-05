import Fastify, { FastifyInstance } from "fastify";

import { AppConfig } from "./config/env";
import { closeDb } from "./db/client";
import { registerGeoWorker } from "./modules/geo/geo-worker";
import { registerErrorHandler } from "./plugins/error-handler";
import { routes } from "./routes";

declare module "fastify" {
  interface FastifyInstance {
    config: AppConfig;
  }
}

export function buildApp(config: AppConfig): FastifyInstance {
  const app = Fastify({
    logger: { level: config.logLevel },
    trustProxy: config.trustProxy,
  });

  app.decorate("config", config);
  registerErrorHandler(app);
  app.register(routes);
  registerGeoWorker(app);

  // Registered after the geo worker so its timer is stopped before the pool goes away.
  app.addHook("onClose", async () => {
    await closeDb();
  });

  return app;
}
