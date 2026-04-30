"use client";

import Link from "next/link";
import { useEffect, useState } from "react";
import { PeriodSelector } from "@/components/PeriodSelector";
import { SensorChart } from "@/components/SensorChart";
import type { MeasurementsResponse, Period } from "@/lib/api";
import { fetchJson } from "@/lib/api";

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
              {publicId}
            </h1>
          </div>

          <PeriodSelector value={period} onChange={setPeriod} />
        </header>

        <section className="mt-5 flex flex-wrap items-center justify-between gap-3">
          <p className="text-sm text-slate-600">
            {isLoading && "Loading measurements..."}
            {!isLoading &&
              state.status === "ready" &&
              `${sensors.length} sensor${sensors.length === 1 ? "" : "s"} for selected period`}
            {!isLoading && state.status === "error" && state.message}
          </p>
        </section>

        {sensors.length === 0 && !isLoading ? (
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
          {sensors.map((sensor) => (
            <SensorChart key={sensor.sensor_type} sensor={sensor} />
          ))}
        </div>
      </div>
    </main>
  );
}
