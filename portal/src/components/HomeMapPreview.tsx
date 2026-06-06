"use client";

import maplibregl from "maplibre-gl";
import { useEffect, useMemo, useRef, useState } from "react";
import { useTranslations } from "next-intl";
import type { DeviceReading, DeviceSummary, DevicesResponse } from "@/lib/api";
import { fetchJson } from "@/lib/api";
import { MAP_STYLE } from "@/lib/map-style";

const PREVIEW_SOURCE_ID = "air360-home-preview-devices";
const PREVIEW_CIRCLE_LAYER_ID = "air360-home-preview-circles";
const PREVIEW_LABEL_LAYER_ID = "air360-home-preview-labels";
const PREVIEW_METRIC = "pm2_5_ug_m3";

type PreviewFeature = {
  type: "Feature";
  geometry: {
    type: "Point";
    coordinates: [number, number];
  };
  properties: {
    color: string;
    label: string;
    opacity: number;
    ringColor: string;
    strokeWidth: number;
  };
};

type PreviewFeatureCollection = {
  type: "FeatureCollection";
  features: PreviewFeature[];
};

export function HomeMapPreview() {
  const t = useTranslations("home");
  const mapContainerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const featureCollectionRef = useRef<PreviewFeatureCollection>(
    emptyFeatureCollection(),
  );
  const [devices, setDevices] = useState<DeviceSummary[]>([]);
  const featureCollection = useMemo(
    () => buildPreviewFeatureCollection(devices),
    [devices],
  );

  useEffect(() => {
    featureCollectionRef.current = featureCollection;
  }, [featureCollection]);

  useEffect(() => {
    const controller = new AbortController();

    fetchJson<DevicesResponse>("/v1/devices", controller.signal)
      .then((data) => setDevices(data.devices))
      .catch(() => {
        if (!controller.signal.aborted) {
          setDevices([]);
        }
      });

    return () => controller.abort();
  }, []);

  useEffect(() => {
    const container = mapContainerRef.current;

    if (!container || mapRef.current) {
      return;
    }

    const map = new maplibregl.Map({
      attributionControl: { compact: true },
      center: [20, 35],
      container,
      doubleClickZoom: false,
      dragPan: false,
      dragRotate: false,
      interactive: false,
      keyboard: false,
      maxZoom: 15,
      pitchWithRotate: false,
      scrollZoom: false,
      style: MAP_STYLE,
      touchZoomRotate: false,
      zoom: 1.5,
    });

    mapRef.current = map;

    map.on("load", () => {
      map.addSource(PREVIEW_SOURCE_ID, {
        type: "geojson",
        data: emptyFeatureCollection(),
      });

      map.addLayer({
        id: PREVIEW_CIRCLE_LAYER_ID,
        type: "circle",
        source: PREVIEW_SOURCE_ID,
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
        id: PREVIEW_LABEL_LAYER_ID,
        type: "symbol",
        source: PREVIEW_SOURCE_ID,
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

      syncPreviewMap(map, featureCollectionRef.current);
    });

    return () => {
      mapRef.current?.remove();
      mapRef.current = null;
    };
  }, []);

  useEffect(() => {
    if (!mapRef.current?.isStyleLoaded()) {
      return;
    }

    syncPreviewMap(mapRef.current, featureCollection);
  }, [featureCollection]);

  return (
    <div className="air-map-preview" aria-label={t("mapPreviewAria")}>
      <div ref={mapContainerRef} className="air-map-preview-canvas" />
    </div>
  );
}

function syncPreviewMap(
  map: maplibregl.Map,
  featureCollection: PreviewFeatureCollection,
) {
  const source = map.getSource(PREVIEW_SOURCE_ID) as
    | maplibregl.GeoJSONSource
    | undefined;

  source?.setData(featureCollection);
  fitPreviewDevices(map, featureCollection.features);
}

function fitPreviewDevices(map: maplibregl.Map, features: PreviewFeature[]) {
  if (features.length === 0) {
    map.easeTo({ center: [20, 35], zoom: 1.5 });
    return;
  }

  if (features.length === 1) {
    map.easeTo({ center: features[0].geometry.coordinates, zoom: 9 });
    return;
  }

  const bounds = new maplibregl.LngLatBounds();

  for (const feature of features) {
    bounds.extend(feature.geometry.coordinates);
  }

  map.fitBounds(bounds, {
    maxZoom: 8,
    padding: { bottom: 72, left: 48, right: 48, top: 72 },
  });
}

function buildPreviewFeatureCollection(
  devices: DeviceSummary[],
): PreviewFeatureCollection {
  return {
    type: "FeatureCollection",
    features: devices.map((device) => {
      const reading = findReading(device, PREVIEW_METRIC);
      const colors = pm25Colors(reading?.value);

      return {
        type: "Feature",
        geometry: {
          type: "Point",
          coordinates: [device.location.longitude, device.location.latitude],
        },
        properties: {
          color: colors.color,
          label: typeof reading?.value === "number" ? markerValue(reading.value) : "",
          opacity: 0.94,
          ringColor: colors.ring,
          strokeWidth: 3,
        },
      };
    }),
  };
}

function emptyFeatureCollection(): PreviewFeatureCollection {
  return {
    type: "FeatureCollection",
    features: [],
  };
}

function findReading(
  device: DeviceSummary,
  kind: string,
): DeviceReading | undefined {
  for (const sensor of device.sensors) {
    const reading = sensor.readings.find((candidate) => candidate.kind === kind);

    if (reading) {
      return reading;
    }
  }

  return undefined;
}

function pm25Colors(value: number | undefined): { color: string; ring: string } {
  if (typeof value !== "number") {
    return { color: "#64748b", ring: "#e2e8f0" };
  }

  if (value <= 12) return { color: "#15803d", ring: "#bbf7d0" };
  if (value <= 35.4) return { color: "#ca8a04", ring: "#fef08a" };
  if (value <= 55.4) return { color: "#ea580c", ring: "#fed7aa" };
  if (value <= 150.4) return { color: "#be123c", ring: "#fecdd3" };
  return { color: "#7f1d1d", ring: "#fecaca" };
}

function markerValue(value: number): string {
  return new Intl.NumberFormat(undefined, {
    maximumFractionDigits: 1,
  }).format(value);
}
