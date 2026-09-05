export interface AppConfig {
  host: string;
  port: number;
  logLevel: string;
  databaseUrl: string;
  /** Fastify `trustProxy`: `true`/`false`, or an address list such as `127.0.0.1,loopback`. */
  trustProxy: boolean | string;
}

const DEFAULT_HOST = "0.0.0.0";
const DEFAULT_PORT = 3000;
const DEFAULT_LOG_LEVEL = "info";
// The reference deployment sits behind nginx on the same host, so proxy
// headers are trusted unless TRUST_PROXY says otherwise.
const DEFAULT_TRUST_PROXY = true;

function parsePort(value: string | undefined): number {
  if (!value) {
    return DEFAULT_PORT;
  }

  const port = Number.parseInt(value, 10);
  if (Number.isNaN(port) || port <= 0 || port > 65535) {
    throw new Error(`Invalid PORT value: ${value}`);
  }

  return port;
}

function parseTrustProxy(value: string | undefined): boolean | string {
  const normalized = value?.trim();
  if (!normalized) {
    return DEFAULT_TRUST_PROXY;
  }
  if (normalized === "true" || normalized === "1") {
    return true;
  }
  if (normalized === "false" || normalized === "0") {
    return false;
  }
  return normalized;
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
    trustProxy: parseTrustProxy(env.TRUST_PROXY),
  };
}
