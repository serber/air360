"use client";

import { useEffect, useMemo, useState } from "react";
import L from "leaflet";
import { MapContainer, Marker, Popup, TileLayer, useMap } from "react-leaflet";
import { DevicePopup } from "@/components/DevicePopup";
import type { DeviceSummary, DevicesResponse } from "@/lib/api";
import { fetchJson } from "@/lib/api";

const deviceIcon = L.divIcon({
  className: "air360-device-marker",
  html: "<span></span>",
  iconAnchor: [14, 14],
  iconSize: [28, 28],
  popupAnchor: [0, -14],
});

type LoadState =
  | { status: "loading"; devices: DeviceSummary[]; message?: never }
  | { status: "ready"; devices: DeviceSummary[]; message?: never }
  | { status: "error"; devices: DeviceSummary[]; message: string };

export function DeviceMap() {
  const [state, setState] = useState<LoadState>({
    status: "loading",
    devices: [],
  });

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

  const mapPosition = useMemo(() => {
    if (state.devices.length === 0) {
      return { center: [20, 0] as [number, number], zoom: 2 };
    }

    const total = state.devices.reduce(
      (acc, device) => {
        acc.latitude += device.location.latitude;
        acc.longitude += device.location.longitude;
        return acc;
      },
      { latitude: 0, longitude: 0 },
    );

    return {
      center: [
        total.latitude / state.devices.length,
        total.longitude / state.devices.length,
      ] as [number, number],
      zoom: state.devices.length === 1 ? 10 : 3,
    };
  }, [state.devices]);

  return (
    <section className="relative min-h-screen bg-[#e6ece8]">
      <div className="absolute left-4 right-4 top-4 z-[500] flex flex-wrap items-start justify-between gap-3 md:left-6 md:right-6 md:top-6">
        <div className="max-w-xl rounded-md border border-slate-200 bg-white/95 px-4 py-3 shadow-lg backdrop-blur">
          <p className="text-xs font-semibold uppercase tracking-[0.16em] text-emerald-700">
            Air360 public portal
          </p>
          <h1 className="mt-1 text-xl font-semibold text-slate-950">
            Device measurements map
          </h1>
        </div>

        <div className="rounded-md border border-slate-200 bg-white/95 px-4 py-3 text-sm text-slate-700 shadow-lg backdrop-blur">
          {state.status === "loading" && "Loading devices..."}
          {state.status === "ready" &&
            `${state.devices.length} device${state.devices.length === 1 ? "" : "s"}`}
          {state.status === "error" && state.message}
        </div>
      </div>

      <MapContainer
        center={mapPosition.center}
        className="h-screen min-h-[640px] w-full"
        scrollWheelZoom
        zoom={mapPosition.zoom}
      >
        <MapViewport devices={state.devices} />
        <TileLayer
          attribution='&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
          url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
        />

        {state.devices.map((device) => (
          <Marker
            icon={deviceIcon}
            key={device.public_id}
            position={[device.location.latitude, device.location.longitude]}
          >
            <Popup>
              <DevicePopup device={device} />
            </Popup>
          </Marker>
        ))}
      </MapContainer>

      {state.status === "ready" && state.devices.length === 0 ? (
        <div className="absolute inset-x-4 bottom-6 z-[500] mx-auto max-w-md rounded-md border border-slate-200 bg-white/95 px-4 py-3 text-sm text-slate-700 shadow-lg backdrop-blur">
          No devices with valid coordinates were returned by the backend.
        </div>
      ) : null}
    </section>
  );
}

function MapViewport({ devices }: { devices: DeviceSummary[] }) {
  const map = useMap();

  useEffect(() => {
    if (devices.length === 0) {
      map.setView([20, 0], 2);
      return;
    }

    if (devices.length === 1) {
      const [device] = devices;
      map.setView([device.location.latitude, device.location.longitude], 10);
      return;
    }

    const bounds = L.latLngBounds(
      devices.map((device) => [
        device.location.latitude,
        device.location.longitude,
      ]),
    );

    map.fitBounds(bounds, { padding: [48, 48] });
  }, [devices, map]);

  return null;
}

function hasValidLocation(device: DeviceSummary): boolean {
  return (
    Number.isFinite(device.location.latitude) &&
    Number.isFinite(device.location.longitude) &&
    Math.abs(device.location.latitude) <= 90 &&
    Math.abs(device.location.longitude) <= 180
  );
}
