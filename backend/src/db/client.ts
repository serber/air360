import { Kysely, PostgresDialect } from "kysely";
import { Pool } from "pg";

import type { Database } from "./schema";
import type { AppConfig } from "../config/env";

let db: Kysely<Database> | null = null;

export function getDb(config: AppConfig): Kysely<Database> {
  if (!db) {
    db = new Kysely<Database>({
      dialect: new PostgresDialect({
        pool: new Pool({
          connectionString: config.databaseUrl,
        }),
      }),
    });
  }
  return db;
}
