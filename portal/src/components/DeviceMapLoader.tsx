"use client";

import dynamic from "next/dynamic";

const DeviceMap = dynamic(
  () => import("@/components/DeviceMap").then((module) => module.DeviceMap),
  {
    loading: () => (
      <main className="flex min-h-screen items-center justify-center bg-[#e6ece8] px-4 text-slate-700">
        Loading map...
      </main>
    ),
    ssr: false,
  },
);

export function DeviceMapLoader() {
  return <DeviceMap />;
}
