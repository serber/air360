import Fastify, { FastifyInstance } from "fastify";

import { AppConfig } from "./config/env";
import { registerErrorHandler } from "./plugins/error-handler";
import { routes } from "./routes";

declare module "fastify" {
  interface FastifyInstance {
    config: AppConfig;
  }
}

export function buildApp(config: AppConfig): FastifyInstance {
  const app = Fastify({
    logger: {
      level: config.logLevel,
    },
  });

  app.decorate("config", config);
  registerErrorHandler(app);
  app.register(routes);

  return app;
}
