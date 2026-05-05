"use client";

import maplibregl from "maplibre-gl";
import { useEffect, useMemo, useRef, useState, type CSSProperties } from "react";
import { createRoot, type Root } from "react-dom/client";
import { DevicePopup } from "@/components/DevicePopup";
import type { DeviceReading, DeviceSummary, DevicesResponse } from "@/lib/api";
import { fetchJson } from "@/lib/api";

const DEVICE_SOURCE_ID = "air360-devices";
const CLUSTER_CIRCLE_LAYER_ID = "air360-cluster-circles";
const CLUSTER_LABEL_LAYER_ID = "air360-cluster-labels";
const DEVICE_CIRCLE_LAYER_ID = "air360-device-circles";
const DEVICE_LABEL_LAYER_ID = "air360-device-labels";
const OFFLINE_DEVICE_SOURCE_ID = "air360-offline-devices";
const OFFLINE_CLUSTER_CIRCLE_LAYER_ID = "air360-offline-cluster-circles";
const OFFLINE_CLUSTER_LABEL_LAYER_ID = "air360-offline-cluster-labels";
const OFFLINE_DEVICE_CIRCLE_LAYER_ID = "air360-offline-device-circles";
const OFFLINE_DEVICE_LABEL_LAYER_ID = "air360-offline-device-labels";
const DEFAULT_MAP_CENTER: [number, number] = [0, 20];
const DEFAULT_MAP_ZOOM = 2;

const MAP_STYLE: maplibregl.StyleSpecification = {
  version: 8,
  glyphs: "https://demotiles.maplibre.org/font/{fontstack}/{range}.pbf",
  sources: {
    osm: {
      type: "raster",
      tiles: ["https://tile.openstreetmap.org/{z}/{x}/{y}.png"],
      tileSize: 256,
      attribution:
        '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    },
  },
  layers: [
    {
      id: "osm",
      type: "raster",
      source: "osm",
    },
  ],
};

const MAP_METRICS = [
  { value: "humidity_percent", label: "Humidity" },
  { value: "pressure_hpa", label: "Pressure" },
  { value: "temperature_c", label: "Temperature" },
  { value: "co2_ppm", label: "CO2" },
  { value: "pm10_0_ug_m3", label: "PM10" },
  { value: "pm1_0_ug_m3", label: "PM1.0" },
  { value: "pm2_5_ug_m3", label: "PM2.5" },
  { value: "pm4_0_ug_m3", label: "PM4.0" },
  { value: "illuminance_lux", label: "Light" },
] as const;

type MapMetric = (typeof MAP_METRICS)[number]["value"];

type QualityLevel =
  | "good"
  | "moderate"
  | "poor"
  | "bad"
  | "neutral"
  | "no-data";
type FreshnessLevel =
  | "fresh"
  | "hour-day"
  | "day-week"
  | "week-month"
  | "month-plus"
  | "unknown";

type DeviceFeatureProperties = {
  public_id: string;
  label: string;
  metricCount: number;
  metricSum: number;
  metricValue: number;
  quality: QualityLevel;
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

type FeatureCollectionOptions = {
  offline?: boolean;
};

type HashMapView = {
  center: [number, number];
  zoom: number;
};

const QUALITY_COLORS: Record<QualityLevel, { color: string; ring: string }> = {
  good: { color: "#15803d", ring: "#bbf7d0" },
  moderate: { color: "#ca8a04", ring: "#fef08a" },
  poor: { color: "#ea580c", ring: "#fed7aa" },
  bad: { color: "#be123c", ring: "#fecdd3" },
  neutral: { color: "#2563eb", ring: "#bfdbfe" },
  "no-data": { color: "#64748b", ring: "#e2e8f0" },
};

const STALE_DEVICE_COLORS = { color: "#6b7280", ring: "#d1d5db" };

const TEMPERATURE_COLOR_STOPS = [
  { label: "< -20", max: -20, color: "#312e81", ring: "#c7d2fe" },
  { label: "-20..-10", max: -10, color: "#1e3a8a", ring: "#bfdbfe" },
  { label: "-10..0", max: 0, color: "#2563eb", ring: "#bfdbfe" },
  { label: "0..10", max: 10, color: "#0891b2", ring: "#a5f3fc" },
  { label: "10..18", max: 18, color: "#0f766e", ring: "#99f6e4" },
  { label: "18..24", max: 24, color: "#16a34a", ring: "#bbf7d0" },
  { label: "24..28", max: 28, color: "#ca8a04", ring: "#fef08a" },
  { label: "28..32", max: 32, color: "#ea580c", ring: "#fed7aa" },
  { label: "32..40", max: 40, color: "#be123c", ring: "#fecdd3" },
  { label: "> 40", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
] as const;

const PRESSURE_COLOR_STOPS = [
  { label: "< 980", max: 980, color: "#4338ca", ring: "#c7d2fe" },
  { label: "980..1000", max: 1000, color: "#2563eb", ring: "#bfdbfe" },
  { label: "1000..1010", max: 1010, color: "#0891b2", ring: "#a5f3fc" },
  { label: "1010..1020", max: 1020, color: "#16a34a", ring: "#bbf7d0" },
  { label: "1020..1030", max: 1030, color: "#ca8a04", ring: "#fef08a" },
  { label: "1030..1040", max: 1040, color: "#ea580c", ring: "#fed7aa" },
  { label: "> 1040", max: Infinity, color: "#be123c", ring: "#fecdd3" },
] as const;

const HUMIDITY_COLOR_STOPS = [
  { label: "< 20", max: 20, color: "#ea580c", ring: "#fed7aa" },
  { label: "20..30", max: 30, color: "#ca8a04", ring: "#fef08a" },
  { label: "30..60", max: 60, color: "#15803d", ring: "#bbf7d0" },
  { label: "60..75", max: 75, color: "#0f766e", ring: "#99f6e4" },
  { label: "75..90", max: 90, color: "#2563eb", ring: "#bfdbfe" },
  { label: "> 90", max: Infinity, color: "#4338ca", ring: "#c7d2fe" },
] as const;

const CO2_COLOR_STOPS = [
  { label: "< 380", max: 380, color: "#7c3aed", ring: "#ddd6fe" },
  { label: "380..450", max: 450, color: "#0f766e", ring: "#99f6e4" },
  { label: "450..600", max: 600, color: "#15803d", ring: "#bbf7d0" },
  { label: "600..800", max: 800, color: "#ca8a04", ring: "#fef08a" },
  { label: "800..1000", max: 1000, color: "#ea580c", ring: "#fed7aa" },
  { label: "> 1000", max: Infinity, color: "#be123c", ring: "#fecdd3" },
] as const;

const PM25_COLOR_STOPS = [
  { label: "<= 5", max: 5, color: "#047857", ring: "#a7f3d0" },
  { label: "5..9", max: 9, color: "#16a34a", ring: "#bbf7d0" },
  { label: "9.1..15", max: 15, color: "#ca8a04", ring: "#fef08a" },
  { label: "15.1..35.4", max: 35.4, color: "#ea580c", ring: "#fed7aa" },
  { label: "35.5..55.4", max: 55.4, color: "#dc2626", ring: "#fecaca" },
  { label: "55.5..125.4", max: 125.4, color: "#be123c", ring: "#fecdd3" },
  { label: "125.5..225.4", max: 225.4, color: "#7e22ce", ring: "#e9d5ff" },
  { label: "> 225.5", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
] as const;

const PM10_COLOR_STOPS = [
  { label: "<= 15", max: 15, color: "#047857", ring: "#a7f3d0" },
  { label: "15..45", max: 45, color: "#16a34a", ring: "#bbf7d0" },
  { label: "45..54", max: 54, color: "#ca8a04", ring: "#fef08a" },
  { label: "55..154", max: 154, color: "#ea580c", ring: "#fed7aa" },
  { label: "155..254", max: 254, color: "#dc2626", ring: "#fecaca" },
  { label: "255..354", max: 354, color: "#be123c", ring: "#fecdd3" },
  { label: "355..424", max: 424, color: "#7e22ce", ring: "#e9d5ff" },
  { label: "> 425", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
] as const;

const PM1_COLOR_STOPS = [
  { label: "<= 3", max: 3, color: "#047857", ring: "#a7f3d0" },
  { label: "3..6", max: 6, color: "#16a34a", ring: "#bbf7d0" },
  { label: "6..10", max: 10, color: "#ca8a04", ring: "#fef08a" },
  { label: "10..20", max: 20, color: "#ea580c", ring: "#fed7aa" },
  { label: "20..35", max: 35, color: "#dc2626", ring: "#fecaca" },
  { label: "35..55", max: 55, color: "#7e22ce", ring: "#e9d5ff" },
  { label: "> 55", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
] as const;

const PM4_COLOR_STOPS = [
  { label: "<= 8", max: 8, color: "#047857", ring: "#a7f3d0" },
  { label: "8..15", max: 15, color: "#16a34a", ring: "#bbf7d0" },
  { label: "15..25", max: 25, color: "#ca8a04", ring: "#fef08a" },
  { label: "25..50", max: 50, color: "#ea580c", ring: "#fed7aa" },
  { label: "50..100", max: 100, color: "#dc2626", ring: "#fecaca" },
  { label: "100..200", max: 200, color: "#7e22ce", ring: "#e9d5ff" },
  { label: "> 200", max: Infinity, color: "#7f1d1d", ring: "#fecaca" },
] as const;

const LIGHT_COLOR_STOPS = [
  { label: "< 10", max: 10, color: "#1e1b4b", ring: "#c7d2fe" },
  { label: "10..200", max: 200, color: "#2563eb", ring: "#bfdbfe" },
  { label: "200..1000", max: 1000, color: "#0f766e", ring: "#99f6e4" },
  { label: "1000..10000", max: 10000, color: "#ca8a04", ring: "#fef08a" },
  { label: "> 10000", max: Infinity, color: "#ea580c", ring: "#fed7aa" },
] as const;

const FRESHNESS_LABELS: Record<FreshnessLevel, string> = {
  fresh: "< 1h",
  "hour-day": "1h-1d",
  "day-week": "1d-7d",
  "week-month": "7d-30d",
  "month-plus": "> 30d",
  unknown: "Unknown",
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
  const [state, setState] = useState<LoadState>({
    status: "loading",
    devices: [],
  });
  const [offlineState, setOfflineState] = useState<OfflineLoadState>({
    status: "idle",
    devices: [],
  });
  const [metric, setMetric] = useState<MapMetric>("pm2_5_ug_m3");
  const [showOfflineDevices, setShowOfflineDevices] = useState(false);
  const [isMapReady, setIsMapReady] = useState(false);
  const mapContainerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const devicesByIdRef = useRef<Map<string, DeviceSummary>>(new Map());
  const popupRootRef = useRef<Root | null>(null);
  const offlineControllerRef = useRef<AbortController | null>(null);
  const hashViewRef = useRef(false);

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
    () => buildFeatureCollection(onlineDevices, metric),
    [metric, onlineDevices],
  );

  const offlineFeatureCollection = useMemo(
    () =>
      buildFeatureCollection(showOfflineDevices ? offlineDevices : [], metric, {
        offline: true,
      }),
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

    map.on("load", () => {
      map.addSource(DEVICE_SOURCE_ID, {
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
      map.addSource(OFFLINE_DEVICE_SOURCE_ID, {
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
        id: CLUSTER_CIRCLE_LAYER_ID,
        type: "circle",
        source: DEVICE_SOURCE_ID,
        filter: ["has", "point_count"],
        paint: {
          "circle-color": clusterColorExpression("pm2_5_ug_m3") as never,
          "circle-opacity": 0.72,
          "circle-radius": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            ["step", ["get", "point_count"], 10, 10, 12, 50, 14, 150, 16],
            6,
            ["step", ["get", "point_count"], 13, 10, 15, 50, 17, 150, 19],
            11,
            ["step", ["get", "point_count"], 16, 10, 18, 50, 21, 150, 24],
          ],
          "circle-stroke-color": clusterRingColorExpression(
            "pm2_5_ug_m3",
          ) as never,
          "circle-stroke-opacity": 0.88,
          "circle-stroke-width": 2,
        },
      });

      map.addLayer({
        id: CLUSTER_LABEL_LAYER_ID,
        type: "symbol",
        source: DEVICE_SOURCE_ID,
        filter: ["has", "point_count"],
        layout: {
          "text-allow-overlap": true,
          "text-field": clusterLabelExpression() as never,
          "text-font": ["Open Sans Bold"],
          "text-size": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            9,
            6,
            10,
            11,
            12,
          ],
        },
        paint: {
          "text-color": "#ffffff",
          "text-halo-color": "rgba(15, 23, 42, 0.35)",
          "text-halo-width": 1,
        },
      });

      map.addLayer({
        id: DEVICE_CIRCLE_LAYER_ID,
        type: "circle",
        source: DEVICE_SOURCE_ID,
        filter: ["!", ["has", "point_count"]],
        paint: {
          "circle-color": ["get", "color"],
          "circle-opacity": ["get", "opacity"],
          "circle-radius": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            8,
            5,
            10,
            8,
            13,
            12,
            17,
            16,
            23,
          ],
          "circle-stroke-color": ["get", "ringColor"],
          "circle-stroke-opacity": ["get", "opacity"],
          "circle-stroke-width": ["get", "strokeWidth"],
        },
      });

      map.addLayer({
        id: DEVICE_LABEL_LAYER_ID,
        type: "symbol",
        source: DEVICE_SOURCE_ID,
        filter: ["!", ["has", "point_count"]],
        layout: {
          "text-allow-overlap": true,
          "text-field": ["get", "label"],
          "text-font": ["Open Sans Bold"],
          "text-size": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            7,
            5,
            8,
            8,
            9,
            13,
            11,
            16,
            12,
          ],
        },
        paint: {
          "text-color": "#ffffff",
          "text-halo-color": "rgba(15, 23, 42, 0.35)",
          "text-halo-width": 1,
          "text-opacity": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            0,
            4,
            0,
            6,
            1,
          ],
        },
      });

      map.addLayer({
        id: OFFLINE_CLUSTER_CIRCLE_LAYER_ID,
        type: "circle",
        source: OFFLINE_DEVICE_SOURCE_ID,
        filter: ["has", "point_count"],
        paint: {
          "circle-color": STALE_DEVICE_COLORS.color,
          "circle-opacity": 0.56,
          "circle-radius": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            ["step", ["get", "point_count"], 10, 10, 12, 50, 14, 150, 16],
            6,
            ["step", ["get", "point_count"], 13, 10, 15, 50, 17, 150, 19],
            11,
            ["step", ["get", "point_count"], 16, 10, 18, 50, 21, 150, 24],
          ],
          "circle-stroke-color": STALE_DEVICE_COLORS.ring,
          "circle-stroke-opacity": 0.76,
          "circle-stroke-width": 2,
        },
      });

      map.addLayer({
        id: OFFLINE_CLUSTER_LABEL_LAYER_ID,
        type: "symbol",
        source: OFFLINE_DEVICE_SOURCE_ID,
        filter: ["has", "point_count"],
        layout: {
          "text-allow-overlap": true,
          "text-field": ["to-string", ["get", "point_count"]],
          "text-font": ["Open Sans Bold"],
          "text-size": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            9,
            6,
            10,
            11,
            12,
          ],
        },
        paint: {
          "text-color": "#ffffff",
          "text-halo-color": "rgba(15, 23, 42, 0.35)",
          "text-halo-width": 1,
        },
      });

      map.addLayer({
        id: OFFLINE_DEVICE_CIRCLE_LAYER_ID,
        type: "circle",
        source: OFFLINE_DEVICE_SOURCE_ID,
        filter: ["!", ["has", "point_count"]],
        paint: {
          "circle-color": ["get", "color"],
          "circle-opacity": ["get", "opacity"],
          "circle-radius": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            8,
            5,
            10,
            8,
            13,
            12,
            17,
            16,
            23,
          ],
          "circle-stroke-color": ["get", "ringColor"],
          "circle-stroke-opacity": ["get", "opacity"],
          "circle-stroke-width": ["get", "strokeWidth"],
        },
      });

      map.addLayer({
        id: OFFLINE_DEVICE_LABEL_LAYER_ID,
        type: "symbol",
        source: OFFLINE_DEVICE_SOURCE_ID,
        filter: ["!", ["has", "point_count"]],
        layout: {
          "text-allow-overlap": true,
          "text-field": ["get", "label"],
          "text-font": ["Open Sans Bold"],
          "text-size": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            7,
            5,
            8,
            8,
            9,
            13,
            11,
            16,
            12,
          ],
        },
        paint: {
          "text-color": "#ffffff",
          "text-halo-color": "rgba(15, 23, 42, 0.35)",
          "text-halo-width": 1,
          "text-opacity": [
            "interpolate",
            ["linear"],
            ["zoom"],
            2,
            0,
            4,
            0,
            6,
            1,
          ],
        },
      });

      map.on("mouseenter", CLUSTER_CIRCLE_LAYER_ID, () => {
        map.getCanvas().style.cursor = "pointer";
      });

      map.on("mouseleave", CLUSTER_CIRCLE_LAYER_ID, () => {
        map.getCanvas().style.cursor = "";
      });

      map.on("click", CLUSTER_CIRCLE_LAYER_ID, (event) => {
        const feature = event.features?.[0];
        const clusterId = feature?.properties?.cluster_id;

        if (typeof clusterId !== "number") {
          return;
        }

        const source = map.getSource(DEVICE_SOURCE_ID) as maplibregl.GeoJSONSource;

        source.getClusterExpansionZoom(clusterId).then((zoom) => {
          map.easeTo({
            center: event.lngLat,
            zoom: Math.min(zoom + 0.5, 13),
          });
        });
      });

      map.on("mouseenter", OFFLINE_CLUSTER_CIRCLE_LAYER_ID, () => {
        map.getCanvas().style.cursor = "pointer";
      });

      map.on("mouseleave", OFFLINE_CLUSTER_CIRCLE_LAYER_ID, () => {
        map.getCanvas().style.cursor = "";
      });

      map.on("click", OFFLINE_CLUSTER_CIRCLE_LAYER_ID, (event) => {
        const feature = event.features?.[0];
        const clusterId = feature?.properties?.cluster_id;

        if (typeof clusterId !== "number") {
          return;
        }

        const source = map.getSource(
          OFFLINE_DEVICE_SOURCE_ID,
        ) as maplibregl.GeoJSONSource;

        source.getClusterExpansionZoom(clusterId).then((zoom) => {
          map.easeTo({
            center: event.lngLat,
            zoom: Math.min(zoom + 0.5, 13),
          });
        });
      });

      map.on("mouseenter", DEVICE_CIRCLE_LAYER_ID, () => {
        map.getCanvas().style.cursor = "pointer";
      });

      map.on("mouseleave", DEVICE_CIRCLE_LAYER_ID, () => {
        map.getCanvas().style.cursor = "";
      });

      map.on("click", DEVICE_CIRCLE_LAYER_ID, (event) => {
        const feature = event.features?.[0];
        const publicId = feature?.properties?.public_id;

        if (typeof publicId !== "string") {
          return;
        }

        const device = devicesByIdRef.current.get(publicId);

        if (!device) {
          return;
        }

        popupRootRef.current?.unmount();

        const popupElement = document.createElement("div");
        const popupRoot = createRoot(popupElement);
        popupRoot.render(<DevicePopup device={device} />);
        popupRootRef.current = popupRoot;

        const popup = new maplibregl.Popup({
          closeButton: true,
          closeOnClick: true,
          maxWidth: "340px",
        })
          .setLngLat([device.location.longitude, device.location.latitude])
          .setDOMContent(popupElement);

        popup.on("close", () => {
          popupRoot.unmount();
          if (popupRootRef.current === popupRoot) {
            popupRootRef.current = null;
          }
        });

        popup.addTo(map);
      });

      map.on("mouseenter", OFFLINE_DEVICE_CIRCLE_LAYER_ID, () => {
        map.getCanvas().style.cursor = "pointer";
      });

      map.on("mouseleave", OFFLINE_DEVICE_CIRCLE_LAYER_ID, () => {
        map.getCanvas().style.cursor = "";
      });

      map.on("click", OFFLINE_DEVICE_CIRCLE_LAYER_ID, (event) => {
        const feature = event.features?.[0];
        const publicId = feature?.properties?.public_id;

        if (typeof publicId !== "string") {
          return;
        }

        const device = devicesByIdRef.current.get(publicId);

        if (!device) {
          return;
        }

        popupRootRef.current?.unmount();

        const popupElement = document.createElement("div");
        const popupRoot = createRoot(popupElement);
        popupRoot.render(<DevicePopup device={device} />);
        popupRootRef.current = popupRoot;

        const popup = new maplibregl.Popup({
          closeButton: true,
          closeOnClick: true,
          maxWidth: "340px",
        })
          .setLngLat([device.location.longitude, device.location.latitude])
          .setDOMContent(popupElement);

        popup.on("close", () => {
          popupRoot.unmount();
          if (popupRootRef.current === popupRoot) {
            popupRootRef.current = null;
          }
        });

        popup.addTo(map);
      });

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
    const source = map?.getSource(DEVICE_SOURCE_ID);
    const offlineSource = map?.getSource(OFFLINE_DEVICE_SOURCE_ID);

    if (
      !isMapReady ||
      !source ||
      !offlineSource ||
      !("setData" in source) ||
      !("setData" in offlineSource)
    ) {
      return;
    }

    const geoJsonSource = source as maplibregl.GeoJSONSource;
    const offlineGeoJsonSource = offlineSource as maplibregl.GeoJSONSource;

    geoJsonSource.setData(
      onlineFeatureCollection as unknown as Parameters<
        maplibregl.GeoJSONSource["setData"]
      >[0],
    );
    offlineGeoJsonSource.setData(
      offlineFeatureCollection as unknown as Parameters<
        maplibregl.GeoJSONSource["setData"]
      >[0],
    );
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

    if (!isMapReady || !map || !map.getLayer(CLUSTER_CIRCLE_LAYER_ID)) {
      return;
    }

    map.setPaintProperty(
      CLUSTER_CIRCLE_LAYER_ID,
      "circle-color",
      clusterColorExpression(metric),
    );
    map.setPaintProperty(
      CLUSTER_CIRCLE_LAYER_ID,
      "circle-stroke-color",
      clusterRingColorExpression(metric),
    );
    map.setLayoutProperty(
      CLUSTER_LABEL_LAYER_ID,
      "text-field",
      clusterLabelExpression(),
    );
  }, [isMapReady, metric]);

  return (
    <section className="relative min-h-screen bg-[#e6ece8]">
      <div className="absolute left-4 top-4 z-[500] flex max-w-sm flex-col gap-3 md:left-6 md:top-6">
        <div className="rounded-md border border-slate-200 bg-white/95 px-4 py-3 text-sm text-slate-700 shadow-lg backdrop-blur">
          {state.status === "loading" && "Loading devices..."}
          {state.status === "ready" &&
            `${visibleDevices.length} device${visibleDevices.length === 1 ? "" : "s"}`}
          {state.status === "error" && state.message}
        </div>

        <div className="rounded-md border border-slate-200 bg-white/95 px-4 py-3 text-xs text-slate-600 shadow-lg backdrop-blur">
          {metric === "temperature_c" ? (
            <TemperatureLegend />
          ) : metric === "pressure_hpa" ? (
            <PressureLegend />
          ) : metric === "humidity_percent" ? (
            <HumidityLegend />
          ) : metric === "co2_ppm" ? (
            <Co2Legend />
          ) : metric === "pm2_5_ug_m3" ? (
            <Pm25Legend />
          ) : metric === "pm10_0_ug_m3" ? (
            <Pm10Legend />
          ) : metric === "pm1_0_ug_m3" ? (
            <Pm1Legend />
          ) : metric === "pm4_0_ug_m3" ? (
            <Pm4Legend />
          ) : metric === "illuminance_lux" ? (
            <LightLegend />
          ) : (
            <div className="grid grid-cols-2 gap-2">
              <LegendItem className="air360-quality-good" label="Good" />
              <LegendItem
                className="air360-quality-moderate"
                label="Moderate"
              />
              <LegendItem className="air360-quality-poor" label="Poor" />
              <LegendItem className="air360-quality-bad" label="Bad" />
              <LegendItem className="air360-quality-neutral" label="Data only" />
              <LegendItem className="air360-quality-no-data" label="No data" />
            </div>
          )}
          <div className="mt-3 flex flex-wrap gap-2 border-t border-slate-200 pt-3">
            {(
              [
                "fresh",
                "hour-day",
                "day-week",
                "week-month",
                "month-plus",
              ] as FreshnessLevel[]
            ).map((freshness) => (
              <span
                className={`air360-freshness-chip air360-fresh-${freshness}`}
                key={freshness}
              >
                {FRESHNESS_LABELS[freshness]}
              </span>
            ))}
          </div>
        </div>
      </div>

      <div className="absolute bottom-6 left-4 z-[500] max-h-[42vh] w-[min(520px,calc(100vw-2rem))] overflow-y-auto rounded-md border border-slate-200 bg-white/95 px-4 py-3 shadow-lg backdrop-blur md:left-6">
        <p className="text-xs font-semibold uppercase tracking-[0.16em] text-emerald-700">
          Map display
        </p>
        <h1 className="mt-1 text-xl font-semibold text-slate-950">
          Measurement layer
        </h1>
        <div className="mt-4 flex flex-wrap gap-2" aria-label="Map metric">
          {MAP_METRICS.map((option) => (
            <button
              aria-pressed={metric === option.value}
              className="rounded-md border border-slate-300 bg-white px-3 py-2 text-xs font-semibold text-slate-700 transition hover:border-slate-950 hover:text-slate-950 aria-pressed:border-slate-950 aria-pressed:bg-slate-950 aria-pressed:text-white"
              key={option.value}
              onClick={() => setMetric(option.value)}
              type="button"
            >
              {option.label}
            </button>
          ))}
        </div>
        <label className="mt-4 flex items-center gap-2 border-t border-slate-200 pt-3 text-sm font-medium text-slate-700">
          <input
            checked={showOfflineDevices}
            className="h-4 w-4 rounded border-slate-300 text-slate-950"
            onChange={(event) => {
              const checked = event.target.checked;

              if (checked) {
                loadOfflineDevices();
              }

              setShowOfflineDevices(checked);
            }}
            type="checkbox"
          />
          Show offline devices
        </label>
        {showOfflineDevices && offlineState.status === "loading" ? (
          <p className="mt-2 text-xs text-slate-500">Loading offline devices...</p>
        ) : null}
        {showOfflineDevices && offlineState.status === "error" ? (
          <p className="mt-2 text-xs text-rose-700">{offlineState.message}</p>
        ) : null}
      </div>

      <div ref={mapContainerRef} className="h-screen min-h-[640px] w-full" />

      {state.status === "ready" &&
      visibleDevices.length === 0 &&
      offlineState.status !== "loading" ? (
        <div className="absolute inset-x-4 bottom-6 z-[500] mx-auto max-w-md rounded-md border border-slate-200 bg-white/95 px-4 py-3 text-sm text-slate-700 shadow-lg backdrop-blur">
          No active devices with valid coordinates were returned by the backend.
        </div>
      ) : null}
    </section>
  );
}

function LegendItem({
  className,
  label,
}: {
  className: string;
  label: string;
}) {
  return (
    <span className="flex items-center gap-2">
      <span className={`air360-legend-dot ${className}`} />
      {label}
    </span>
  );
}

function NoDataLegendItem() {
  return (
    <div className="mt-3 border-t border-slate-200 pt-3 text-slate-500">
      <span className="flex items-center gap-2">
        <span className="air360-legend-dot air360-quality-no-data" />
        No data
      </span>
    </div>
  );
}

function TemperatureLegend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">Temperature</span>
        <span className="text-slate-500">cold to hot</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #312e81, #1e3a8a, #2563eb, #0891b2, #0f766e, #16a34a, #ca8a04, #ea580c, #be123c, #7f1d1d)",
        }}
      />
      <div className="mt-2 grid grid-cols-4 gap-2">
        {TEMPERATURE_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function PressureLegend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">Pressure</span>
        <span className="text-slate-500">low to high</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #4338ca, #2563eb, #0891b2, #16a34a, #ca8a04, #ea580c, #be123c)",
        }}
      />
      <div className="mt-2 grid grid-cols-3 gap-2">
        {PRESSURE_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function HumidityLegend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">Humidity</span>
        <span className="text-slate-500">dry to humid</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #ea580c, #ca8a04, #15803d, #0f766e, #2563eb, #4338ca)",
        }}
      />
      <div className="mt-2 grid grid-cols-3 gap-2">
        {HUMIDITY_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function Co2Legend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">CO2</span>
        <span className="text-slate-500">baseline to high</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #7c3aed, #0f766e, #15803d, #ca8a04, #ea580c, #be123c)",
        }}
      />
      <div className="mt-2 grid grid-cols-3 gap-2">
        {CO2_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function Pm25Legend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">PM2.5</span>
        <span className="text-slate-500">clean to hazardous</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #047857, #16a34a, #ca8a04, #ea580c, #dc2626, #be123c, #7e22ce, #7f1d1d)",
        }}
      />
      <div className="mt-2 grid grid-cols-3 gap-2">
        {PM25_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function Pm10Legend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">PM10</span>
        <span className="text-slate-500">clean to hazardous</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #047857, #16a34a, #ca8a04, #ea580c, #dc2626, #be123c, #7e22ce, #7f1d1d)",
        }}
      />
      <div className="mt-2 grid grid-cols-3 gap-2">
        {PM10_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function Pm1Legend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">PM1.0</span>
        <span className="text-slate-500">clean to hazardous</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #047857, #16a34a, #ca8a04, #ea580c, #dc2626, #7e22ce, #7f1d1d)",
        }}
      />
      <div className="mt-2 grid grid-cols-3 gap-2">
        {PM1_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function Pm4Legend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">PM4.0</span>
        <span className="text-slate-500">clean to hazardous</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #047857, #16a34a, #ca8a04, #ea580c, #dc2626, #7e22ce, #7f1d1d)",
        }}
      />
      <div className="mt-2 grid grid-cols-3 gap-2">
        {PM4_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function LightLegend() {
  return (
    <div>
      <div className="flex items-center justify-between gap-3">
        <span className="font-semibold text-slate-700">Light</span>
        <span className="text-slate-500">dark to bright</span>
      </div>
      <div
        aria-hidden="true"
        className="mt-2 h-2 rounded-full"
        style={{
          background:
            "linear-gradient(90deg, #1e1b4b, #2563eb, #0f766e, #ca8a04, #ea580c)",
        }}
      />
      <div className="mt-2 grid grid-cols-2 gap-2">
        {LIGHT_COLOR_STOPS.map((stop) => (
          <span className="flex items-center gap-2" key={stop.label}>
            <span
              className="air360-legend-dot"
              style={
                {
                  "--marker-bg": stop.color,
                  "--marker-ring": stop.ring,
                } as CSSProperties
              }
            />
            {stop.label}
          </span>
        ))}
      </div>
      <NoDataLegendItem />
    </div>
  );
}

function buildFeatureCollection(
  devices: DeviceSummary[],
  metric: MapMetric,
  options: FeatureCollectionOptions = {},
): DeviceFeatureCollection {
  return {
    type: "FeatureCollection",
    features: devices.map((device) => {
      const reading = options.offline ? undefined : findReading(device, metric);
      const quality = reading ? qualityLevel(metric, reading.value) : "no-data";
      const freshness = freshnessLevel(device.last_seen_at);
      const freshnessStyle = FRESHNESS_STYLES[freshness];
      const colors = options.offline
        ? STALE_DEVICE_COLORS
        : metricColors(metric, reading, quality);
      const metricValue = reading?.value ?? 0;
      const metricCount = reading ? 1 : 0;
      let label = "-";

      if (options.offline) {
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
          metricCount,
          metricSum: metricValue,
          metricValue,
          quality,
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

function metricColors(
  metric: MapMetric,
  reading: DeviceReading | undefined,
  quality: QualityLevel,
): { color: string; ring: string } {
  if (!reading) {
    return QUALITY_COLORS[quality];
  }

  if (metric === "temperature_c") {
    return temperatureColors(reading.value);
  }

  if (metric === "pressure_hpa") {
    return pressureColors(reading.value);
  }

  if (metric === "humidity_percent") {
    return humidityColors(reading.value);
  }

  if (metric === "co2_ppm") {
    return co2Colors(reading.value);
  }

  if (metric === "pm2_5_ug_m3") {
    return pm25Colors(reading.value);
  }

  if (metric === "pm10_0_ug_m3") {
    return pm10Colors(reading.value);
  }

  if (metric === "pm1_0_ug_m3") {
    return pm1Colors(reading.value);
  }

  if (metric === "pm4_0_ug_m3") {
    return pm4Colors(reading.value);
  }

  if (metric === "illuminance_lux") {
    return lightColors(reading.value);
  }

  return QUALITY_COLORS[quality];
}

function emptyFeatureCollection(): DeviceFeatureCollection {
  return {
    type: "FeatureCollection",
    features: [],
  };
}

function clusterLabelExpression() {
  const avg = clusterAverageExpression();

  return [
    "case",
    [">", ["get", "metricCount"], 0],
    ["to-string", ["round", avg]],
    "",
  ];
}

function clusterColorExpression(metric: MapMetric) {
  return clusterQualityExpression(metric, "color");
}

function clusterRingColorExpression(metric: MapMetric) {
  return clusterQualityExpression(metric, "ring");
}

function clusterQualityExpression(
  metric: MapMetric,
  colorKind: keyof (typeof QUALITY_COLORS)[QualityLevel],
) {
  const color = (level: QualityLevel) => QUALITY_COLORS[level][colorKind];

  if (metric === "pm10_0_ug_m3") {
    return clusterPm10Expression(colorKind);
  }

  if (metric === "pm2_5_ug_m3") {
    return clusterPm25Expression(colorKind);
  }

  if (metric === "pm1_0_ug_m3") {
    return clusterPm1Expression(colorKind);
  }

  if (metric === "pm4_0_ug_m3") {
    return clusterPm4Expression(colorKind);
  }

  if (metric === "co2_ppm") {
    return clusterCo2Expression(colorKind);
  }

  if (metric === "temperature_c") {
    return clusterTemperatureExpression(colorKind);
  }

  if (metric === "pressure_hpa") {
    return clusterPressureExpression(colorKind);
  }

  if (metric === "humidity_percent") {
    return clusterHumidityExpression(colorKind);
  }

  if (metric === "illuminance_lux") {
    return clusterLightExpression(colorKind);
  }

  return [
    "case",
    [">", ["get", "metricCount"], 0],
    color("neutral"),
    color("no-data"),
  ];
}

function clusterAverageExpression() {
  return ["/", ["get", "metricSum"], ["get", "metricCount"]];
}

function clusterTemperatureExpression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof TEMPERATURE_COLOR_STOPS)[number]) =>
    stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<", avg, -20],
    color(TEMPERATURE_COLOR_STOPS[0]),
    ["<=", avg, -10],
    color(TEMPERATURE_COLOR_STOPS[1]),
    ["<=", avg, 0],
    color(TEMPERATURE_COLOR_STOPS[2]),
    ["<=", avg, 10],
    color(TEMPERATURE_COLOR_STOPS[3]),
    ["<=", avg, 18],
    color(TEMPERATURE_COLOR_STOPS[4]),
    ["<=", avg, 24],
    color(TEMPERATURE_COLOR_STOPS[5]),
    ["<=", avg, 28],
    color(TEMPERATURE_COLOR_STOPS[6]),
    ["<=", avg, 32],
    color(TEMPERATURE_COLOR_STOPS[7]),
    ["<=", avg, 40],
    color(TEMPERATURE_COLOR_STOPS[8]),
    color(TEMPERATURE_COLOR_STOPS[9]),
  ];
}

function temperatureColors(value: number): { color: string; ring: string } {
  if (value < -20) {
    return TEMPERATURE_COLOR_STOPS[0];
  }

  return (
    TEMPERATURE_COLOR_STOPS.slice(1).find((stop) => value <= stop.max) ??
    TEMPERATURE_COLOR_STOPS[TEMPERATURE_COLOR_STOPS.length - 1]
  );
}

function clusterPressureExpression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof PRESSURE_COLOR_STOPS)[number]) =>
    stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<", avg, 980],
    color(PRESSURE_COLOR_STOPS[0]),
    ["<=", avg, 1000],
    color(PRESSURE_COLOR_STOPS[1]),
    ["<=", avg, 1010],
    color(PRESSURE_COLOR_STOPS[2]),
    ["<=", avg, 1020],
    color(PRESSURE_COLOR_STOPS[3]),
    ["<=", avg, 1030],
    color(PRESSURE_COLOR_STOPS[4]),
    ["<=", avg, 1040],
    color(PRESSURE_COLOR_STOPS[5]),
    color(PRESSURE_COLOR_STOPS[6]),
  ];
}

function pressureColors(value: number): { color: string; ring: string } {
  if (value < 980) {
    return PRESSURE_COLOR_STOPS[0];
  }

  return (
    PRESSURE_COLOR_STOPS.slice(1).find((stop) => value <= stop.max) ??
    PRESSURE_COLOR_STOPS[PRESSURE_COLOR_STOPS.length - 1]
  );
}

function clusterHumidityExpression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof HUMIDITY_COLOR_STOPS)[number]) =>
    stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<", avg, 20],
    color(HUMIDITY_COLOR_STOPS[0]),
    ["<=", avg, 30],
    color(HUMIDITY_COLOR_STOPS[1]),
    ["<=", avg, 60],
    color(HUMIDITY_COLOR_STOPS[2]),
    ["<=", avg, 75],
    color(HUMIDITY_COLOR_STOPS[3]),
    ["<=", avg, 90],
    color(HUMIDITY_COLOR_STOPS[4]),
    color(HUMIDITY_COLOR_STOPS[5]),
  ];
}

function humidityColors(value: number): { color: string; ring: string } {
  if (value < 20) {
    return HUMIDITY_COLOR_STOPS[0];
  }

  return (
    HUMIDITY_COLOR_STOPS.slice(1).find((stop) => value <= stop.max) ??
    HUMIDITY_COLOR_STOPS[HUMIDITY_COLOR_STOPS.length - 1]
  );
}

function clusterCo2Expression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof CO2_COLOR_STOPS)[number]) => stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<", avg, 380],
    color(CO2_COLOR_STOPS[0]),
    ["<=", avg, 450],
    color(CO2_COLOR_STOPS[1]),
    ["<=", avg, 600],
    color(CO2_COLOR_STOPS[2]),
    ["<=", avg, 800],
    color(CO2_COLOR_STOPS[3]),
    ["<=", avg, 1000],
    color(CO2_COLOR_STOPS[4]),
    color(CO2_COLOR_STOPS[5]),
  ];
}

function co2Colors(value: number): { color: string; ring: string } {
  if (value < 380) {
    return CO2_COLOR_STOPS[0];
  }

  return (
    CO2_COLOR_STOPS.slice(1).find((stop) => value <= stop.max) ??
    CO2_COLOR_STOPS[CO2_COLOR_STOPS.length - 1]
  );
}

function clusterPm25Expression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof PM25_COLOR_STOPS)[number]) => stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<=", avg, 5],
    color(PM25_COLOR_STOPS[0]),
    ["<=", avg, 9],
    color(PM25_COLOR_STOPS[1]),
    ["<=", avg, 15],
    color(PM25_COLOR_STOPS[2]),
    ["<=", avg, 35.4],
    color(PM25_COLOR_STOPS[3]),
    ["<=", avg, 55.4],
    color(PM25_COLOR_STOPS[4]),
    ["<=", avg, 125.4],
    color(PM25_COLOR_STOPS[5]),
    ["<=", avg, 225.4],
    color(PM25_COLOR_STOPS[6]),
    color(PM25_COLOR_STOPS[7]),
  ];
}

function pm25Colors(value: number): { color: string; ring: string } {
  return (
    PM25_COLOR_STOPS.find((stop) => value <= stop.max) ??
    PM25_COLOR_STOPS[PM25_COLOR_STOPS.length - 1]
  );
}

function clusterPm10Expression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof PM10_COLOR_STOPS)[number]) => stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<=", avg, 15],
    color(PM10_COLOR_STOPS[0]),
    ["<=", avg, 45],
    color(PM10_COLOR_STOPS[1]),
    ["<=", avg, 54],
    color(PM10_COLOR_STOPS[2]),
    ["<=", avg, 154],
    color(PM10_COLOR_STOPS[3]),
    ["<=", avg, 254],
    color(PM10_COLOR_STOPS[4]),
    ["<=", avg, 354],
    color(PM10_COLOR_STOPS[5]),
    ["<=", avg, 424],
    color(PM10_COLOR_STOPS[6]),
    color(PM10_COLOR_STOPS[7]),
  ];
}

function pm10Colors(value: number): { color: string; ring: string } {
  return (
    PM10_COLOR_STOPS.find((stop) => value <= stop.max) ??
    PM10_COLOR_STOPS[PM10_COLOR_STOPS.length - 1]
  );
}

function clusterPm1Expression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof PM1_COLOR_STOPS)[number]) => stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<=", avg, 3],
    color(PM1_COLOR_STOPS[0]),
    ["<=", avg, 6],
    color(PM1_COLOR_STOPS[1]),
    ["<=", avg, 10],
    color(PM1_COLOR_STOPS[2]),
    ["<=", avg, 20],
    color(PM1_COLOR_STOPS[3]),
    ["<=", avg, 35],
    color(PM1_COLOR_STOPS[4]),
    ["<=", avg, 55],
    color(PM1_COLOR_STOPS[5]),
    color(PM1_COLOR_STOPS[6]),
  ];
}

function pm1Colors(value: number): { color: string; ring: string } {
  return (
    PM1_COLOR_STOPS.find((stop) => value <= stop.max) ??
    PM1_COLOR_STOPS[PM1_COLOR_STOPS.length - 1]
  );
}

function clusterPm4Expression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof PM4_COLOR_STOPS)[number]) => stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<=", avg, 8],
    color(PM4_COLOR_STOPS[0]),
    ["<=", avg, 15],
    color(PM4_COLOR_STOPS[1]),
    ["<=", avg, 25],
    color(PM4_COLOR_STOPS[2]),
    ["<=", avg, 50],
    color(PM4_COLOR_STOPS[3]),
    ["<=", avg, 100],
    color(PM4_COLOR_STOPS[4]),
    ["<=", avg, 200],
    color(PM4_COLOR_STOPS[5]),
    color(PM4_COLOR_STOPS[6]),
  ];
}

function pm4Colors(value: number): { color: string; ring: string } {
  return (
    PM4_COLOR_STOPS.find((stop) => value <= stop.max) ??
    PM4_COLOR_STOPS[PM4_COLOR_STOPS.length - 1]
  );
}

function clusterLightExpression(colorKind: "color" | "ring") {
  const avg = clusterAverageExpression();
  const color = (stop: (typeof LIGHT_COLOR_STOPS)[number]) => stop[colorKind];

  return [
    "case",
    ["<=", ["get", "metricCount"], 0],
    QUALITY_COLORS["no-data"][colorKind],
    ["<", avg, 10],
    color(LIGHT_COLOR_STOPS[0]),
    ["<=", avg, 200],
    color(LIGHT_COLOR_STOPS[1]),
    ["<=", avg, 1000],
    color(LIGHT_COLOR_STOPS[2]),
    ["<=", avg, 10000],
    color(LIGHT_COLOR_STOPS[3]),
    color(LIGHT_COLOR_STOPS[4]),
  ];
}

function lightColors(value: number): { color: string; ring: string } {
  if (value < 10) {
    return LIGHT_COLOR_STOPS[0];
  }

  return (
    LIGHT_COLOR_STOPS.slice(1).find((stop) => value <= stop.max) ??
    LIGHT_COLOR_STOPS[LIGHT_COLOR_STOPS.length - 1]
  );
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

function findReading(
  device: DeviceSummary,
  metric: MapMetric,
): DeviceReading | undefined {
  for (const sensor of device.sensors) {
    const reading = sensor.readings.find(
      (candidate) => candidate.kind === metric,
    );

    if (reading) {
      return reading;
    }
  }

  return undefined;
}

function qualityLevel(metric: MapMetric, value: number): QualityLevel {
  switch (metric) {
    case "pm2_5_ug_m3":
      if (value <= 12) return "good";
      if (value <= 35) return "moderate";
      if (value <= 55) return "poor";
      return "bad";
    case "pm10_0_ug_m3":
      if (value <= 54) return "good";
      if (value <= 154) return "moderate";
      if (value <= 254) return "poor";
      return "bad";
    case "pm1_0_ug_m3":
    case "pm4_0_ug_m3":
      if (value <= 12) return "good";
      if (value <= 35) return "moderate";
      if (value <= 55) return "poor";
      return "bad";
    case "co2_ppm":
      if (value < 800) return "good";
      if (value < 1200) return "moderate";
      if (value < 2000) return "poor";
      return "bad";
    case "temperature_c":
      if (value >= 18 && value <= 26) return "good";
      if (value >= 16 && value <= 30) return "moderate";
      if (value >= 10 && value <= 35) return "poor";
      return "bad";
    case "humidity_percent":
      if (value >= 30 && value <= 60) return "good";
      if (value >= 25 && value <= 70) return "moderate";
      if (value >= 15 && value <= 80) return "poor";
      return "bad";
    case "pressure_hpa":
    case "illuminance_lux":
      return "neutral";
  }
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

function markerValue(metric: MapMetric, value: number): string {
  if (metric === "temperature_c") {
    return Math.round(value).toString();
  }

  if (metric === "humidity_percent") {
    return Math.round(value).toString();
  }

  if (metric === "co2_ppm") {
    return value >= 1000
      ? `${Math.round(value / 100) / 10}k`
      : Math.round(value).toString();
  }

  return Math.round(value).toString();
}

function hasValidLocation(device: DeviceSummary): boolean {
  return (
    Number.isFinite(device.location.latitude) &&
    Number.isFinite(device.location.longitude) &&
    Math.abs(device.location.latitude) <= 90 &&
    Math.abs(device.location.longitude) <= 180
  );
}
