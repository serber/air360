process.env.TZ = "UTC";

import { buildApp } from "./app";
import { loadConfig } from "./config/env";

async function start(): Promise<void> {
  const config = loadConfig();
  const app = buildApp(config);

  try {
    await app.listen({
      host: config.host,
      port: config.port,
    });
  } catch (error) {
    app.log.error(error);
    process.exit(1);
  }
}

void start();
