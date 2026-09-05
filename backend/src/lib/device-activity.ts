import { sql } from "kysely";

/**
 * A device is "online" when its last batch arrived within this window. The
 * portal mirrors the same value in `DEVICE_STALE_AFTER_MS`; change both.
 */
export const DEVICE_ONLINE_WINDOW = "1 hour";

/** SQL fragment for the cut-off timestamp: `NOW() - <window>`. */
export const deviceOnlineSince = sql<Date>`NOW() - ${DEVICE_ONLINE_WINDOW}::interval`;
