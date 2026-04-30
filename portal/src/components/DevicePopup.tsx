"use client";

import Link from "next/link";
import type { DeviceSummary } from "@/lib/api";
import { formatDateTime, formatValue, kindLabel, sensorLabel } from "@/lib/api";

type DevicePopupProps = {
  device: DeviceSummary;
};

export function DevicePopup({ device }: DevicePopupProps) {
  return (
    <div className="w-72 max-w-[72vw] text-slate-900">
      <div>
        <p className="text-xs font-semibold uppercase tracking-[0.14em] text-emerald-700">
          Air360 device
        </p>
        <h2 className="mt-1 text-base font-semibold text-slate-950">
          {device.name}
        </h2>
        <p className="mt-1 text-xs text-slate-500">
          Last seen {formatDateTime(device.last_seen_at)}
        </p>
      </div>

      <div className="mt-4 max-h-64 overflow-y-auto pr-1">
        {device.sensors.length === 0 ? (
          <p className="rounded-md bg-slate-100 px-3 py-2 text-sm text-slate-600">
            No measurements yet.
          </p>
        ) : (
          <div className="space-y-4">
            {device.sensors.map((sensor) => (
              <section key={sensor.sensor_type}>
                <h3 className="text-xs font-semibold uppercase tracking-[0.12em] text-slate-500">
                  {sensorLabel(sensor.sensor_type)}
                </h3>
                <dl className="mt-2 space-y-2">
                  {sensor.readings.map((reading) => (
                    <div
                      key={`${sensor.sensor_type}-${reading.kind}`}
                      className="flex items-baseline justify-between gap-3 rounded-md bg-slate-50 px-3 py-2"
                    >
                      <dt className="text-xs text-slate-500">
                        {kindLabel(reading.kind)}
                      </dt>
                      <dd className="text-sm font-semibold text-slate-950">
                        {formatValue(reading.kind, reading.value)}
                      </dd>
                    </div>
                  ))}
                </dl>
              </section>
            ))}
          </div>
        )}
      </div>

      <Link
        href={`/devices/${device.public_id}`}
        className="mt-4 inline-flex w-full items-center justify-center rounded-md bg-slate-950 px-3 py-2 text-sm font-semibold text-white transition hover:bg-slate-800"
      >
        Open device page
      </Link>
    </div>
  );
}
