"use client";

import maplibregl from "maplibre-gl";
import { useEffect, useMemo, useRef, useState } from "react";
import { createRoot, type Root } from "react-dom/client";
import {
  NextIntlClientProvider,
  useLocale,
  useMessages,
  useTranslations,
} from "next-intl";
import { DevicePopup } from "@/components/DevicePopup";
import { MapLegend, type FreshnessLevel } from "@/components/MapLegend";
import { defaultTimeZone } from "@/i18n/request";
import type { DeviceSummary, DevicesResponse } from "@/lib/api";
import { fetchJson, hasValidLocation } from "@/lib/api";
import {
  MAP_METRICS,
  METRIC_SCALES,
  STALE_DEVICE_COLORS,
  clusterColorExpression,
  clusterLabelExpression,
  colorsForValue,
  findMetricReading,
  markerValue,
  type MapMetric,
} from "@/lib/map-scales";
import { MAP_STYLE } from "@/lib/map-style";

const DEFAULT_MAP_CENTER: [number, number] = [0, 20];
const DEFAULT_MAP_ZOOM = 2;
const DEFAULT_METRIC: MapMetric = "pm2_5_ug_m3";

type LayerIds = {
  source: string;
  clusterCircles: string;
  clusterLabels: string;
  deviceCircles: string;
  deviceLabels: string;
};

const ONLINE_LAYERS: LayerIds = {
  source: "air360-devices",
  clusterCircles: "air360-cluster-circles",
  clusterLabels: "air360-cluster-labels",
  deviceCircles: "air360-device-circles",
  deviceLabels: "air360-device-labels",
};

const OFFLINE_LAYERS: LayerIds = {
  source: "air360-offline-devices",
  clusterCircles: "air360-offline-cluster-circles",
  clusterLabels: "air360-offline-cluster-labels",
  deviceCircles: "air360-offline-device-circles",
  deviceLabels: "air360-offline-device-labels",
};

type DeviceFeatureProperties = {
  public_id: string;
  label: string;
  metricCount: number;
  metricSum: number;
  freshness: FreshnessLevel;
  color: string;
  opacity: number;
  ringColor: string;
  strokeWidth: number;
};

type DeviceFeature = {
  type: "Feature";
  geometry: {
    type: "Point";
    coordinates: [number, number];
  };
  properties: DeviceFeatureProperties;
};

type DeviceFeatureCollection = {
  type: "FeatureCollection";
  features: DeviceFeature[];
};

type HashMapView = {
  center: [number, number];
  zoom: number;
};

const FRESHNESS_STYLES: Record<
  FreshnessLevel,
  { opacity: number; strokeWidth: number }
> = {
  fresh: { opacity: 0.94, strokeWidth: 3 },
  "hour-day": { opacity: 0.78, strokeWidth: 2.5 },
  "day-week": { opacity: 0.62, strokeWidth: 2 },
  "week-month": { opacity: 0.46, strokeWidth: 1.5 },
  "month-plus": { opacity: 0.3, strokeWidth: 1.2 },
  unknown: { opacity: 0.3, strokeWidth: 1.2 },
};

type LoadState =
  | { status: "loading"; devices: DeviceSummary[]; message?: never }
  | { status: "ready"; devices: DeviceSummary[]; message?: never }
  | { status: "error"; devices: DeviceSummary[]; message: string };

type OfflineLoadState =
  | { status: "idle"; devices: DeviceSummary[]; message?: never }
  | { status: "loading"; devices: DeviceSummary[]; message?: never }
  | { status: "ready"; devices: DeviceSummary[]; message?: never }
  | { status: "error"; devices: DeviceSummary[]; message: string };

export function DeviceMap() {
  const t = useTranslations("mapPage");
  const locale = useLocale();
  const messages = useMessages();
  const [state, setState] = useState<LoadState>({
    status: "loading",
    devices: [],
  });
  const [offlineState, setOfflineState] = useState<OfflineLoadState>({
    status: "idle",
    devices: [],
  });
  const [metric, setMetric] = useState<MapMetric>(DEFAULT_METRIC);
  const [showOfflineDevices, setShowOfflineDevices] = useState(false);
  const [isMapReady, setIsMapReady] = useState(false);
  const mapContainerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const devicesByIdRef = useRef<Map<string, DeviceSummary>>(new Map());
  const popupRootRef = useRef<Root | null>(null);
  const offlineControllerRef = useRef<AbortController | null>(null);
  const hashViewRef = useRef(false);
  const localeRef = useRef(locale);
  const messagesRef = useRef(messages);

  useEffect(() => {
    localeRef.current = locale;
    messagesRef.current = messages;
  }, [locale, messages]);

  useEffect(() => {
    const controller = new AbortController();

    fetchJson<DevicesResponse>("/v1/devices", controller.signal)
      .then((data) => {
        setState({
          status: "ready",
          devices: data.devices.filter(hasValidLocation),
        });
      })
      .catch((error: unknown) => {
        if (controller.signal.aborted) return;

        setState({
          status: "error",
          devices: [],
          message:
            error instanceof Error
              ? error.message
              : "Unable to load devices.",
        });
      });

    return () => controller.abort();
  }, []);

  function loadOfflineDevices() {
    if (offlineState.status === "loading" || offlineState.status === "ready") {
      return;
    }

    offlineControllerRef.current?.abort();
    const controller = new AbortController();
    offlineControllerRef.current = controller;

    setOfflineState({ status: "loading", devices: [] });

    fetchJson<DevicesResponse>("/v1/devices/offline", controller.signal)
      .then((data) => {
        setOfflineState({
          status: "ready",
          devices: data.devices.filter(hasValidLocation),
        });
      })
      .catch((error: unknown) => {
        if (controller.signal.aborted) return;

        setOfflineState({
          status: "error",
          devices: [],
          message:
            error instanceof Error
              ? error.message
              : "Unable to load offline devices.",
        });
      });
  }

  useEffect(() => {
    return () => offlineControllerRef.current?.abort();
  }, []);

  useEffect(() => {
    devicesByIdRef.current = new Map(
      [...state.devices, ...offlineState.devices].map((device) => [
        device.public_id,
        device,
      ]),
    );
  }, [offlineState.devices, state.devices]);

  const onlineDevices = state.devices;
  const offlineDevices = offlineState.devices;

  const visibleDevices = useMemo(
    () =>
      showOfflineDevices
        ? [...onlineDevices, ...offlineDevices]
        : onlineDevices,
    [offlineDevices, onlineDevices, showOfflineDevices],
  );

  const onlineFeatureCollection = useMemo(
    () => buildFeatureCollection(onlineDevices, metric, false),
    [metric, onlineDevices],
  );

  const offlineFeatureCollection = useMemo(
    () =>
      buildFeatureCollection(showOfflineDevices ? offlineDevices : [], metric, true),
    [metric, offlineDevices, showOfflineDevices],
  );

  useEffect(() => {
    if (!mapContainerRef.current || mapRef.current) {
      return;
    }

    const hashView = parseMapHash(window.location.hash);
    hashViewRef.current = Boolean(hashView);

    const map = new maplibregl.Map({
      center: hashView?.center ?? DEFAULT_MAP_CENTER,
      container: mapContainerRef.current,
      maxZoom: 15,
      style: MAP_STYLE,
      zoom: hashView?.zoom ?? DEFAULT_MAP_ZOOM,
    });

    mapRef.current = map;
    map.addControl(new maplibregl.NavigationControl({ visualizePitch: false }));

    const syncHash = () => updateMapHash(map);
    const handleHashChange = () => {
      const nextHashView = parseMapHash(window.location.hash);

      if (!nextHashView) {
        return;
      }

      hashViewRef.current = true;
      map.easeTo({
        center: nextHashView.center,
        zoom: nextHashView.zoom,
      });
    };

    map.on("moveend", syncHash);
    window.addEventListener("hashchange", handleHashChange);

    const openDevicePopup = (publicId: string) => {
      const device = devicesByIdRef.current.get(publicId);

      if (!device) {
        return;
      }

      popupRootRef.current?.unmount();

      const popupElement = document.createElement("div");
      const popup = new maplibregl.Popup({
        className: "air-map-popup",
        closeButton: false,
        closeOnClick: true,
        maxWidth: "340px",
      })
        .setLngLat([device.location.longitude, device.location.latitude])
        .setDOMContent(popupElement);
      const popupRoot = createRoot(popupElement);
      popupRoot.render(
        <NextIntlClientProvider
          locale={localeRef.current}
          messages={messagesRef.current}
          timeZone={defaultTimeZone}
        >
          <DevicePopup device={device} onClose={() => popup.remove()} />
        </NextIntlClientProvider>,
      );
      popupRootRef.current = popupRoot;

      popup.on("close", () => {
        popupRoot.unmount();
        if (popupRootRef.current === popupRoot) {
          popupRootRef.current = null;
        }
      });

      popup.addTo(map);
    };

    map.on("load", () => {
      const initialScale = METRIC_SCALES[DEFAULT_METRIC];

      addDeviceLayers(map, ONLINE_LAYERS, {
        clusterColor: clusterColorExpression(initialScale, "color"),
        clusterRingColor: clusterColorExpression(initialScale, "ring"),
        clusterOpacity: 0.72,
        clusterRingOpacity: 0.88,
        clusterLabel: clusterLabelExpression(),
      });
      addDeviceLayers(map, OFFLINE_LAYERS, {
        clusterColor: STALE_DEVICE_COLORS.color,
        clusterRingColor: STALE_DEVICE_COLORS.ring,
        clusterOpacity: 0.56,
        clusterRingOpacity: 0.76,
        clusterLabel: ["to-string", ["get", "point_count"]],
      });

      bindLayerInteractions(map, ONLINE_LAYERS, openDevicePopup);
      bindLayerInteractions(map, OFFLINE_LAYERS, openDevicePopup);

      setIsMapReady(true);
    });

    return () => {
      popupRootRef.current?.unmount();
      popupRootRef.current = null;
      window.removeEventListener("hashchange", handleHashChange);
      map.off("moveend", syncHash);
      map.remove();
      mapRef.current = null;
    };
  }, []);

  useEffect(() => {
    const map = mapRef.current;

    if (!isMapReady || !map) {
      return;
    }

    setSourceData(map, ONLINE_LAYERS.source, onlineFeatureCollection);
    setSourceData(map, OFFLINE_LAYERS.source, offlineFeatureCollection);
  }, [isMapReady, offlineFeatureCollection, onlineFeatureCollection]);

  useEffect(() => {
    const map = mapRef.current;

    if (!isMapReady || !map || hashViewRef.current) {
      return;
    }

    fitDevices(map, visibleDevices);
  }, [isMapReady, visibleDevices]);

  useEffect(() => {
    const map = mapRef.current;

    if (!isMapReady || !map || !map.getLayer(ONLINE_LAYERS.clusterCircles)) {
      return;
    }

    const scale = METRIC_SCALES[metric];
    map.setPaintProperty(
      ONLINE_LAYERS.clusterCircles,
      "circle-color",
      clusterColorExpression(scale, "color"),
    );
    map.setPaintProperty(
      ONLINE_LAYERS.clusterCircles,
      "circle-stroke-color",
      clusterColorExpression(scale, "ring"),
    );
  }, [isMapReady, metric]);

  return (
    <section className="air-map-page">
      <div className="air-map-control-stack">
        <div className="air-map-status-panel">
          <span className="air-live-dot" />
          <span>
            {state.status === "loading" && t("loadingDevices")}
            {state.status === "ready" &&
              t("deviceCount", { count: visibleDevices.length })}
            {state.status === "error" && state.message}
          </span>
        </div>

        <MapLegend scale={METRIC_SCALES[metric]} />
      </div>

      <div className="air-map-layer-stack">
        <h1 className="air-map-layer-title">{t("measurementLayer")}</h1>
        <div className="air-map-layer-panel" aria-label={t("metricAria")}>
          {MAP_METRICS.map((option) => (
            <button
              aria-pressed={metric === option}
              className="air-map-layer-button"
              key={option}
              onClick={() => setMetric(option)}
              type="button"
            >
              <span>{t(`metrics.${option}`)}</span>
            </button>
          ))}
        </div>

        <label className="air-map-offline-toggle">
          <input
            checked={showOfflineDevices}
            onChange={(event) => {
              const checked = event.target.checked;

              if (checked) {
                loadOfflineDevices();
              }

              setShowOfflineDevices(checked);
            }}
            type="checkbox"
          />
          {t("showOfflineDevices")}
        </label>
        {showOfflineDevices && offlineState.status === "loading" ? (
          <p className="air-map-control-note">{t("loadingOfflineDevices")}</p>
        ) : null}
        {showOfflineDevices && offlineState.status === "error" ? (
          <p className="air-map-control-error">{offlineState.message}</p>
        ) : null}
      </div>

      <div ref={mapContainerRef} className="air-map-canvas" />

      {state.status === "ready" &&
      visibleDevices.length === 0 &&
      offlineState.status !== "loading" ? (
        <div className="air-map-empty-note">{t("empty")}</div>
      ) : null}
    </section>
  );
}

// Circle radius and label size grow with zoom; shared by every layer set so
// online and offline markers stay the same size.
const CLUSTER_RADIUS = [
  "interpolate",
  ["linear"],
  ["zoom"],
  2,
  ["step", ["get", "point_count"], 10, 10, 12, 50, 14, 150, 16],
  6,
  ["step", ["get", "point_count"], 13, 10, 15, 50, 17, 150, 19],
  11,
  ["step", ["get", "point_count"], 16, 10, 18, 50, 21, 150, 24],
];
const CLUSTER_TEXT_SIZE = ["interpolate", ["linear"], ["zoom"], 2, 9, 6, 10, 11, 12];
const DEVICE_RADIUS = ["interpolate", ["linear"], ["zoom"], 2, 8, 5, 10, 8, 13, 12, 17, 16, 23];
const DEVICE_TEXT_SIZE = ["interpolate", ["linear"], ["zoom"], 2, 7, 5, 8, 8, 9, 13, 11, 16, 12];
const DEVICE_TEXT_OPACITY = ["interpolate", ["linear"], ["zoom"], 2, 0, 4, 0, 6, 1];
const LABEL_PAINT = {
  "text-color": "#ffffff",
  "text-halo-color": "rgba(15, 23, 42, 0.35)",
  "text-halo-width": 1,
};

type ClusterStyle = {
  clusterColor: unknown;
  clusterRingColor: unknown;
  clusterOpacity: number;
  clusterRingOpacity: number;
  clusterLabel: unknown;
};

/** One clustered GeoJSON source plus its cluster and device circle/label layers. */
function addDeviceLayers(map: maplibregl.Map, ids: LayerIds, style: ClusterStyle) {
  map.addSource(ids.source, {
    type: "geojson",
    cluster: true,
    clusterMaxZoom: 11,
    clusterProperties: {
      metricCount: ["+", ["get", "metricCount"]],
      metricSum: ["+", ["get", "metricSum"]],
    },
    clusterRadius: 40,
    data: emptyFeatureCollection(),
  });

  map.addLayer({
    id: ids.clusterCircles,
    type: "circle",
    source: ids.source,
    filter: ["has", "point_count"],
    paint: {
      "circle-color": style.clusterColor as never,
      "circle-opacity": style.clusterOpacity,
      "circle-radius": CLUSTER_RADIUS as never,
      "circle-stroke-color": style.clusterRingColor as never,
      "circle-stroke-opacity": style.clusterRingOpacity,
      "circle-stroke-width": 2,
    },
  });

  map.addLayer({
    id: ids.clusterLabels,
    type: "symbol",
    source: ids.source,
    filter: ["has", "point_count"],
    layout: {
      "text-allow-overlap": true,
      "text-field": style.clusterLabel as never,
      "text-font": ["Open Sans Bold"],
      "text-size": CLUSTER_TEXT_SIZE as never,
    },
    paint: LABEL_PAINT,
  });

  map.addLayer({
    id: ids.deviceCircles,
    type: "circle",
    source: ids.source,
    filter: ["!", ["has", "point_count"]],
    paint: {
      "circle-color": ["get", "color"],
      "circle-opacity": ["get", "opacity"],
      "circle-radius": DEVICE_RADIUS as never,
      "circle-stroke-color": ["get", "ringColor"],
      "circle-stroke-opacity": ["get", "opacity"],
      "circle-stroke-width": ["get", "strokeWidth"],
    },
  });

  map.addLayer({
    id: ids.deviceLabels,
    type: "symbol",
    source: ids.source,
    filter: ["!", ["has", "point_count"]],
    layout: {
      "text-allow-overlap": true,
      "text-field": ["get", "label"],
      "text-font": ["Open Sans Bold"],
      "text-size": DEVICE_TEXT_SIZE as never,
    },
    paint: {
      ...LABEL_PAINT,
      "text-opacity": DEVICE_TEXT_OPACITY as never,
    },
  });
}

/** Pointer cursor, cluster zoom-in on click, device popup on click. */
function bindLayerInteractions(
  map: maplibregl.Map,
  ids: LayerIds,
  openDevicePopup: (publicId: string) => void,
) {
  for (const layerId of [ids.clusterCircles, ids.deviceCircles]) {
    map.on("mouseenter", layerId, () => {
      map.getCanvas().style.cursor = "pointer";
    });
    map.on("mouseleave", layerId, () => {
      map.getCanvas().style.cursor = "";
    });
  }

  map.on("click", ids.clusterCircles, (event) => {
    const clusterId = event.features?.[0]?.properties?.cluster_id;

    if (typeof clusterId !== "number") {
      return;
    }

    const source = map.getSource(ids.source) as maplibregl.GeoJSONSource;

    source.getClusterExpansionZoom(clusterId).then((zoom) => {
      map.easeTo({
        center: event.lngLat,
        zoom: Math.min(zoom + 0.5, 13),
      });
    });
  });

  map.on("click", ids.deviceCircles, (event) => {
    const publicId = event.features?.[0]?.properties?.public_id;

    if (typeof publicId === "string") {
      openDevicePopup(publicId);
    }
  });
}

function setSourceData(
  map: maplibregl.Map,
  sourceId: string,
  collection: DeviceFeatureCollection,
) {
  const source = map.getSource(sourceId);

  if (source && "setData" in source) {
    (source as maplibregl.GeoJSONSource).setData(
      collection as unknown as Parameters<maplibregl.GeoJSONSource["setData"]>[0],
    );
  }
}

function buildFeatureCollection(
  devices: DeviceSummary[],
  metric: MapMetric,
  offline: boolean,
): DeviceFeatureCollection {
  const scale = METRIC_SCALES[metric];

  return {
    type: "FeatureCollection",
    features: devices.map((device) => {
      const reading = offline ? undefined : findMetricReading(device, metric);
      const freshness = freshnessLevel(device.last_seen_at);
      const freshnessStyle = FRESHNESS_STYLES[freshness];
      const colors = offline
        ? STALE_DEVICE_COLORS
        : colorsForValue(scale, reading?.value);
      let label = "-";

      if (offline) {
        label = "";
      } else if (reading) {
        label = markerValue(metric, reading.value);
      }

      return {
        type: "Feature",
        geometry: {
          type: "Point",
          coordinates: [device.location.longitude, device.location.latitude],
        },
        properties: {
          public_id: device.public_id,
          label,
          metricCount: reading ? 1 : 0,
          metricSum: reading?.value ?? 0,
          freshness,
          color: colors.color,
          opacity: freshnessStyle.opacity,
          ringColor: colors.ring,
          strokeWidth: freshnessStyle.strokeWidth,
        },
      };
    }),
  };
}

function emptyFeatureCollection(): DeviceFeatureCollection {
  return {
    type: "FeatureCollection",
    features: [],
  };
}

function parseMapHash(hash: string): HashMapView | null {
  const normalized = hash.replace(/^#/, "");
  const [zoomRaw, latRaw, lngRaw] = normalized.split("/");

  if (!zoomRaw || !latRaw || !lngRaw) {
    return null;
  }

  const zoom = Number(zoomRaw);
  const latitude = Number(latRaw);
  const longitude = Number(lngRaw);

  if (
    !Number.isFinite(zoom) ||
    !Number.isFinite(latitude) ||
    !Number.isFinite(longitude) ||
    zoom < 0 ||
    zoom > 22 ||
    Math.abs(latitude) > 90 ||
    Math.abs(longitude) > 180
  ) {
    return null;
  }

  return {
    center: [longitude, latitude],
    zoom,
  };
}

function updateMapHash(map: maplibregl.Map) {
  const center = map.getCenter();
  const zoom = map.getZoom().toFixed(2);
  const latitude = center.lat.toFixed(5);
  const longitude = center.lng.toFixed(5);
  const nextHash = `#${zoom}/${latitude}/${longitude}`;

  if (window.location.hash === nextHash) {
    return;
  }

  window.history.replaceState(
    null,
    "",
    `${window.location.pathname}${window.location.search}${nextHash}`,
  );
}

function fitDevices(map: maplibregl.Map, devices: DeviceSummary[]) {
  if (devices.length === 0) {
    map.easeTo({ center: DEFAULT_MAP_CENTER, zoom: DEFAULT_MAP_ZOOM });
    return;
  }

  if (devices.length === 1) {
    const [device] = devices;
    map.easeTo({
      center: [device.location.longitude, device.location.latitude],
      zoom: 10,
    });
    return;
  }

  const bounds = new maplibregl.LngLatBounds();

  for (const device of devices) {
    bounds.extend([device.location.longitude, device.location.latitude]);
  }

  map.fitBounds(bounds, {
    padding: { bottom: 220, left: 560, right: 80, top: 160 },
  });
}

function freshnessLevel(lastSeenAt: string): FreshnessLevel {
  const lastSeen = new Date(lastSeenAt).getTime();

  if (Number.isNaN(lastSeen)) {
    return "unknown";
  }

  const ageMs = Date.now() - lastSeen;
  const hourMs = 60 * 60 * 1000;
  const dayMs = 24 * hourMs;

  if (ageMs < hourMs) return "fresh";
  if (ageMs < dayMs) return "hour-day";
  if (ageMs < 7 * dayMs) return "day-week";
  if (ageMs < 30 * dayMs) return "week-month";
  return "month-plus";
}
