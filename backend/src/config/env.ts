export interface AppConfig {
  host: string;
  port: number;
  logLevel: string;
  databaseUrl: string;
}

const DEFAULT_HOST = "0.0.0.0";
const DEFAULT_PORT = 3000;
const DEFAULT_LOG_LEVEL = "info";

function parsePort(value: string | undefined): number {
  if (!value) {
    return DEFAULT_PORT;
  }

  const port = Number.parseInt(value, 10);
  if (Number.isNaN(port) || port <= 0) {
    throw new Error(`Invalid PORT value: ${value}`);
  }

  return port;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): AppConfig {
  const databaseUrl = env.DATABASE_URL;
  if (!databaseUrl) {
    throw new Error("DATABASE_URL environment variable is required");
  }

  return {
    host: env.HOST ?? DEFAULT_HOST,
    port: parsePort(env.PORT),
    logLevel: env.LOG_LEVEL ?? DEFAULT_LOG_LEVEL,
    databaseUrl,
  };
}
