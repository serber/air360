"use client";

import dynamic from "next/dynamic";

const DeviceMap = dynamic(
  () => import("@/components/DeviceMap").then((module) => module.DeviceMap),
  {
    loading: () => (
      <main className="air-map-page flex items-center justify-center px-4 text-[var(--ink-2)]">
        Loading map...
      </main>
    ),
    ssr: false,
  },
);

export function DeviceMapLoader() {
  return <DeviceMap />;
}
