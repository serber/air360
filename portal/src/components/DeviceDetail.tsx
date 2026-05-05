"use client";

import Link from "next/link";
import { useEffect, useState } from "react";
import { PeriodSelector } from "@/components/PeriodSelector";
import { SensorChart } from "@/components/SensorChart";
import type { MeasurementsResponse, Period } from "@/lib/api";
import {
  fetchJson,
  formatDateTime,
  formatValue,
  kindLabel,
  sensorLabel,
} from "@/lib/api";

type DeviceDetailProps = {
  publicId: string;
};

type LoadState =
  | { status: "idle"; data?: MeasurementsResponse; message?: never }
  | { status: "ready"; data: MeasurementsResponse; message?: never }
  | { status: "error"; data?: MeasurementsResponse; message: string };

export function DeviceDetail({ publicId }: DeviceDetailProps) {
  const [period, setPeriod] = useState<Period>("24h");
  const [state, setState] = useState<LoadState>({ status: "idle" });

  useEffect(() => {
    const controller = new AbortController();

    fetchJson<MeasurementsResponse>(
      `/v1/devices/${encodeURIComponent(publicId)}/measurements?period=${period}`,
      controller.signal,
    )
      .then((data) => setState({ status: "ready", data }))
      .catch((error: unknown) => {
        if (controller.signal.aborted) return;

        setState((current) => ({
          status: "error",
          data: current.data,
          message:
            error instanceof Error
              ? error.message
              : "Unable to load measurements.",
        }));
      });

    return () => controller.abort();
  }, [period, publicId]);

  const device = state.data?.device;
  const byKind = state.data?.by_kind ?? [];
  const latest = state.data?.latest ?? [];
  const sensors = state.data?.sensors ?? [];
  const isLoading =
    state.status === "idle" || state.data === undefined || state.data.period !== period;

  return (
    <main className="min-h-screen bg-[#f4f7f5] text-slate-950">
      <div className="mx-auto flex w-full max-w-7xl flex-col px-4 py-5 sm:px-6 lg:px-8">
        <header className="flex flex-col gap-5 border-b border-slate-200 pb-5 lg:flex-row lg:items-end lg:justify-between">
          <div>
            <Link
              className="text-sm font-semibold text-emerald-700 transition hover:text-emerald-900"
              href="/map"
            >
              Back to map
            </Link>
            <p className="mt-5 text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">
              Device
            </p>
            <h1 className="mt-2 break-all text-2xl font-semibold tracking-tight text-slate-950 sm:text-3xl">
              {device?.name ?? publicId}
            </h1>
            {device && (
              <p className="mt-1 font-mono text-sm text-slate-400">{publicId}</p>
            )}
          </div>

          <PeriodSelector value={period} onChange={setPeriod} />
        </header>

        {device && (
          <section className="mt-4 grid grid-cols-2 gap-3 sm:grid-cols-4">
            <div className="rounded-md border border-slate-200 bg-white px-4 py-3 shadow-sm">
              <p className="text-xs font-semibold uppercase tracking-wider text-slate-500">
                Location
              </p>
              <p className="mt-1 text-sm text-slate-950">
                {device.latitude.toFixed(4)}, {device.longitude.toFixed(4)}
              </p>
            </div>
            <div className="rounded-md border border-slate-200 bg-white px-4 py-3 shadow-sm">
              <p className="text-xs font-semibold uppercase tracking-wider text-slate-500">
                Firmware
              </p>
              <p className="mt-1 text-sm text-slate-950">
                {device.firmware_version}
              </p>
            </div>
            <div className="rounded-md border border-slate-200 bg-white px-4 py-3 shadow-sm">
              <p className="text-xs font-semibold uppercase tracking-wider text-slate-500">
                Registered
              </p>
              <p className="mt-1 text-sm text-slate-950">
                {formatDateTime(device.registered_at)}
              </p>
            </div>
            <div className="rounded-md border border-slate-200 bg-white px-4 py-3 shadow-sm">
              <p className="text-xs font-semibold uppercase tracking-wider text-slate-500">
                Last seen
              </p>
              <p className="mt-1 text-sm text-slate-950">
                {formatDateTime(device.last_seen_at)}
              </p>
            </div>
          </section>
        )}

        {latest.length > 0 && (
          <section className="mt-4">
            <h2 className="mb-3 text-sm font-semibold text-slate-700">
              Latest readings
            </h2>
            <div className="flex flex-wrap gap-3">
              {latest.map((r) => (
                <div
                  key={`${r.sensor_type}-${r.kind}`}
                  className="rounded-md border border-slate-200 bg-white px-4 py-3 shadow-sm"
                >
                  <p className="text-xs font-semibold text-emerald-700">
                    {sensorLabel(r.sensor_type)}
                  </p>
                  <p className="mt-0.5 text-xs text-slate-500">
                    {kindLabel(r.kind)}
                  </p>
                  <p className="mt-1 text-lg font-semibold text-slate-950">
                    {formatValue(r.kind, r.value)}
                  </p>
                </div>
              ))}
            </div>
          </section>
        )}

        {sensors.length > 0 && (
          <section className="mt-4">
            <h2 className="mb-3 text-sm font-semibold text-slate-700">
              Sensors
            </h2>
            <div className="flex flex-wrap gap-3">
              {sensors.map((s) => (
                <div
                  key={s.sensor_type}
                  className="rounded-md border border-slate-200 bg-white px-4 py-3 shadow-sm"
                >
                  <p className="text-sm font-semibold text-slate-950">
                    {sensorLabel(s.sensor_type)}
                  </p>
                  <p className="mt-1 text-xs text-slate-500">
                    {s.kinds.map(kindLabel).join(", ")}
                  </p>
                </div>
              ))}
            </div>
          </section>
        )}

        <section className="mt-5 flex flex-wrap items-center justify-between gap-3">
          <p className="text-sm text-slate-600">
            {isLoading && "Loading measurements..."}
            {!isLoading &&
              state.status === "ready" &&
              `${byKind.length} measurement type${byKind.length === 1 ? "" : "s"} for selected period`}
            {!isLoading && state.status === "error" && state.message}
          </p>
        </section>

        {byKind.length === 0 && !isLoading ? (
          <section className="mt-8 rounded-md border border-slate-200 bg-white px-4 py-10 text-center shadow-sm">
            <h2 className="text-lg font-semibold text-slate-950">
              No chart data
            </h2>
            <p className="mt-2 text-sm text-slate-600">
              The backend did not return measurements for this device and
              period.
            </p>
          </section>
        ) : null}

        <div className="mt-6 grid gap-5">
          {byKind.map((k) => (
            <SensorChart key={k.kind} measurement={k} />
          ))}
        </div>
      </div>
    </main>
  );
}
