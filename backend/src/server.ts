process.env.TZ = "UTC";

import { buildApp } from "./app";
import { loadConfig } from "./config/env";

async function start(): Promise<void> {
  const config = loadConfig();
  const app = buildApp(config);

  // systemd stop / restart sends SIGTERM; close the listener, stop the geo
  // worker, and drain the Postgres pool instead of waiting for SIGKILL.
  const shutdown = (signal: NodeJS.Signals): void => {
    app.log.info({ signal }, "shutting down");
    app.close().then(
      () => process.exit(0),
      (error: unknown) => {
        app.log.error(error, "shutdown failed");
        process.exit(1);
      },
    );
  };
  process.once("SIGTERM", shutdown);
  process.once("SIGINT", shutdown);

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
