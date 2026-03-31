import Fastify, { FastifyInstance } from "fastify";

import { AppConfig } from "./config/env";
import { registerErrorHandler } from "./plugins/error-handler";
import { routes } from "./routes";

export function buildApp(config: AppConfig): FastifyInstance {
  const app = Fastify({
    logger: {
      level: config.logLevel,
    },
  });

  registerErrorHandler(app);
  app.register(routes);

  return app;
}
