import { Kysely, PostgresDialect } from "kysely";
import pg, { Pool } from "pg";

import type { Database } from "./schema";
import type { AppConfig } from "../config/env";

// BIGINT (OID 20) is returned as string by default; our values are ≤ 48-bit so safe as number.
pg.types.setTypeParser(20, Number);

let db: Kysely<Database> | null = null;

export function getDb(config: AppConfig): Kysely<Database> {
  if (!db) {
    db = new Kysely<Database>({
      dialect: new PostgresDialect({
        pool: new Pool({
          connectionString: config.databaseUrl,
          options: "-c timezone=UTC",
        }),
      }),
    });
  }
  return db;
}
