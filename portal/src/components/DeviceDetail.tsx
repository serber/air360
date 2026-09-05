"use client";

import Link from "next/link";
import maplibregl from "maplibre-gl";
import { useEffect, useMemo, useRef, useState } from "react";
import { useTranslations } from "next-intl";
import { PeriodSelector } from "@/components/PeriodSelector";
import { SensorChart, type ChartMeasurement } from "@/components/SensorChart";
import type { KindMeasurements, MeasurementsResponse, Period } from "@/lib/api";
import {
  countryCodeFlag,
  fetchJson,
  formatDateTime,
  formatValue,
  isDeviceStale,
  kindLabel,
  sensorLabel,
} from "@/lib/api";
import { CHART_GROUPS, buildGroupedChart } from "@/lib/chart-groups";
import { MAP_STYLE } from "@/lib/map-style";

type DeviceDetailProps = {
  publicId: string;
};

type LoadState =
  | { status: "idle"; data?: MeasurementsResponse; message?: never }
  | { status: "ready"; data: MeasurementsResponse; message?: never }
  | { status: "error"; data?: MeasurementsResponse; message: string };

export function DeviceDetail({ publicId }: DeviceDetailProps) {
  const t = useTranslations("deviceDetail");
  const tGroups = useTranslations("chartGroups");
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
  const byKind = state.data?.by_kind;
  const latest = state.data?.latest ?? [];
  const sensors = state.data?.sensors ?? [];
  // Resolve each group's title once per locale so the memo below keeps a stable
  // identity — SensorChart tears down its canvas whenever `measurement` changes.
  const groupTitles = useMemo(
    () =>
      Object.fromEntries(
        CHART_GROUPS.map((group) => [group.titleKey, tGroups(group.titleKey)]),
      ) as Record<string, string>,
    [tGroups],
  );
  const chartMeasurements = useMemo(
    () => buildChartMeasurements(byKind ?? [], groupTitles),
    [byKind, groupTitles],
  );
  const isStale = device ? isDeviceStale(device.last_seen_at) : true;
  // A failed request must surface its message: never report "loading" once the
  // fetch has settled with an error, even when no data was ever received.
  const isLoading =
    state.status !== "error" &&
    (state.status === "idle" || state.data === undefined || state.data.period !== period);
  const deviceTitle = device?.name?.trim() || t("fallbackTitle");
  const countryFlag = countryCodeFlag(device?.geo_country_code);
  const countryValue = device?.geo_country
    ? `${device.geo_country}${countryFlag ? ` ${countryFlag}` : ""}`
    : (countryFlag ?? "-");
  const deviceMapHref = device
    ? `/map#11/${device.latitude.toFixed(5)}/${device.longitude.toFixed(5)}`
    : "/map";

  return (
    <main className="air-device-page">
      <div className="air-container">
        <header className="air-device-head">
          <div className="air-crumb">
            <Link href="/map">{t("backToMap")}</Link>
            <span>/</span>
            <span>{t("crumb")}</span>
          </div>

          <div className="air-device-title-row">
            <h1>{deviceTitle}</h1>
            {device ? (
              <span className={isStale ? "air-pill-status air-stale" : "air-pill-status"}>
                <span className="air-live-dot" />
                {isStale ? t("stale") : t("online")}
              </span>
            ) : null}
          </div>

          {device ? (
            <div className="air-device-meta">
              <span>
                {t("locationMeta")} <b>{device.latitude.toFixed(4)}, {device.longitude.toFixed(4)}</b>
              </span>
              <span>
                {t("joined")} <b>{formatDateTime(device.registered_at)}</b>
              </span>
              <span>
                {t("lastSeen")} <b>{formatDateTime(device.last_seen_at)}</b>
              </span>
              <span>
                {t("firmware")} <b>{device.firmware_version}</b>
              </span>
            </div>
          ) : null}

          {latest.length > 0 ? (
            <div className="air-now-strip">
              {latest.map((reading) => (
                <div
                  className="air-now-cell"
                  key={`${reading.sensor_type}-${reading.kind}`}
                >
                  <span className="air-now-label">{kindLabel(reading.kind)}</span>
                  <span className="air-now-value">{formatValue(reading.kind, reading.value)}</span>
                  <span className="air-now-source">{sensorLabel(reading.sensor_type)}</span>
                </div>
              ))}
            </div>
          ) : null}
        </header>

        <section className="air-device-control-bar">
          <PeriodSelector value={period} onChange={setPeriod} />
          <p className="air-device-load-state">
            {isLoading && t("loadingMeasurements")}
            {!isLoading &&
              state.status === "ready" &&
              t("chartCount", { count: chartMeasurements.length })}
            {!isLoading && state.status === "error" && state.message}
          </p>
        </section>

        <div className="air-device-panel-grid">
          <div className="air-device-charts">
            {chartMeasurements.length === 0 && !isLoading ? (
              <section className="air-empty-state">
                <h2>{t("noChartTitle")}</h2>
                <p>
                  {t("noChartBody")}
                </p>
              </section>
            ) : null}

            {chartMeasurements.map((k) => (
              <SensorChart key={k.kind} measurement={k} />
            ))}
          </div>

          <aside className="air-device-side">
            <section className="air-info-card">
              <div className="air-info-card-head">
                <h4>{t("location")}</h4>
                <Link href={deviceMapHref}>{t("mapLink")}</Link>
              </div>
              {device ? (
                <DeviceStaticMap
                  latitude={device.latitude}
                  longitude={device.longitude}
                />
              ) : (
                <div className="air-mini-map air-mini-map-empty" />
              )}
              <div className="air-info-card-body">
                <InfoRow label={t("country")} value={countryValue} />
                <InfoRow label={t("city")} value={device?.geo_city ?? t("unknown")} />
                <InfoRow
                  label={t("latitude")}
                  value={device ? device.latitude.toFixed(5) : "-"}
                />
                <InfoRow
                  label={t("longitude")}
                  value={device ? device.longitude.toFixed(5) : "-"}
                />
                <InfoRow
                  label={t("altitude")}
                  value={
                    typeof device?.altitude_m === "number"
                      ? `${device.altitude_m.toFixed(1)} m`
                      : "-"
                  }
                />
              </div>
            </section>

            <section className="air-info-card">
              <div className="air-info-card-head">
                <h4>{t("hardware")}</h4>
              </div>
              <div className="air-sensor-list">
                {sensors.length > 0 ? (
                  sensors.map((sensor) => (
                    <div className="air-sensor-row" key={sensor.sensor_type}>
                      <div>
                        <div className="air-sensor-name">
                          {sensorLabel(sensor.sensor_type)}
                        </div>
                        <div className="air-sensor-desc">
                          {sensor.kinds.map(kindLabel).join(", ")}
                        </div>
                      </div>
                    </div>
                  ))
                ) : (
                  <p className="air-info-empty">{t("noSensors")}</p>
                )}
              </div>
            </section>

          </aside>
        </div>
      </div>
    </main>
  );
}

/**
 * Turns the backend's per-kind measurements into chart cards, merging kinds that
 * a `CHART_GROUPS` entry declares as one physical quantity (PM mass fractions,
 * number-concentration bins, particle counts, the two pressure references). Kinds
 * with no group, and groups with only one member present, render one card each.
 * Original `by_kind` order is preserved; a group takes the slot of its first
 * present member.
 */
function buildChartMeasurements(
  measurements: KindMeasurements[],
  groupTitles: Record<string, string>,
): ChartMeasurement[] {
  const byKind = new Map(measurements.map((m) => [m.kind, m]));
  const groupOfKind = new Map(
    CHART_GROUPS.flatMap((group) => group.kinds.map((kind) => [kind, group] as const)),
  );

  const charts: ChartMeasurement[] = [];
  const emittedGroups = new Set<string>();

  for (const measurement of measurements) {
    const group = groupOfKind.get(measurement.kind);

    if (!group) {
      charts.push(measurement);
      continue;
    }

    if (emittedGroups.has(group.id)) {
      continue;
    }
    emittedGroups.add(group.id);

    const members = group.kinds
      .map((kind) => byKind.get(kind))
      .filter((member): member is KindMeasurements => member !== undefined);

    // A lone member stays a normal per-kind card so its specific label survives.
    if (members.length <= 1) {
      if (members[0]) {
        charts.push(members[0]);
      }
      continue;
    }

    charts.push(buildGroupedChart(group, members, groupTitles[group.titleKey]));
  }

  return charts;
}

function DeviceStaticMap({
  latitude,
  longitude,
}: {
  latitude: number;
  longitude: number;
}) {
  const t = useTranslations("deviceDetail");
  const mapContainerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const markerRef = useRef<maplibregl.Marker | null>(null);

  useEffect(() => {
    const container = mapContainerRef.current;

    if (!container) {
      return;
    }

    const center: [number, number] = [longitude, latitude];
    const markerElement = document.createElement("div");
    markerElement.className = "air-mini-map-pin";

    const map = new maplibregl.Map({
      attributionControl: { compact: true },
      center,
      container,
      doubleClickZoom: false,
      dragPan: false,
      dragRotate: false,
      interactive: false,
      keyboard: false,
      maxZoom: 11,
      minZoom: 11,
      pitchWithRotate: false,
      scrollZoom: false,
      style: MAP_STYLE,
      touchZoomRotate: false,
      zoom: 11,
    });

    const marker = new maplibregl.Marker({
      anchor: "center",
      element: markerElement,
    })
      .setLngLat(center)
      .addTo(map);

    mapRef.current = map;
    markerRef.current = marker;
    map.once("load", () => map.resize());

    return () => {
      markerRef.current?.remove();
      mapRef.current?.remove();
      markerRef.current = null;
      mapRef.current = null;
    };
  }, [latitude, longitude]);

  return (
    <div
      ref={mapContainerRef}
      aria-label={t("deviceLocationMap")}
      className="air-mini-map"
    />
  );
}

function InfoRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="air-info-row">
      <span>{label}</span>
      <b>{value}</b>
    </div>
  );
}
