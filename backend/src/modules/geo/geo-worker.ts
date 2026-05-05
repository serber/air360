import type { FastifyInstance } from "fastify";

import { getDb } from "../../db/client";
import { findDeviceByDeviceId, updateDeviceGeo } from "../devices/device-repository";
import { dequeueGeoUpdate } from "./geo-queue-repository";
import { reverseGeocode } from "./reverse-geocoder";

// Nominatim allows 1 req/sec; 1100 ms gives a safe margin
const TICK_INTERVAL_MS = 1_100;

async function tick(app: FastifyInstance): Promise<void> {
  const db = getDb(app.config);

  const device_id = await dequeueGeoUpdate(db);
  if (device_id === null) return;

  const device = await findDeviceByDeviceId(db, device_id);
  if (!device) return;

  const result = await reverseGeocode(device.latitude, device.longitude);
  if (!result) {
    app.log.warn({ device_id }, "reverse geocode failed, skipping");
    return;
  }

  await updateDeviceGeo(db, device_id, result);
  app.log.info({ device_id, geo_display: result.geo_display }, "geo updated");
}

export function startGeoWorker(app: FastifyInstance): void {
  const timer = setInterval(() => {
    tick(app).catch((err: unknown) =>
      app.log.error({ err }, "geo worker tick error"),
    );
  }, TICK_INTERVAL_MS);

  app.addHook("onClose", (_instance, done) => {
    clearInterval(timer);
    done();
  });
}
