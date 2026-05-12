"use client";

import dynamic from "next/dynamic";
import { useTranslations } from "next-intl";

function DeviceMapLoading() {
  const t = useTranslations("mapPage");

  return (
    <main className="air-map-page flex items-center justify-center px-4 text-[var(--ink-2)]">
      {t("loadingMap")}
    </main>
  );
}

const DeviceMap = dynamic(
  () => import("@/components/DeviceMap").then((module) => module.DeviceMap),
  {
    loading: () => <DeviceMapLoading />,
    ssr: false,
  },
);

export function DeviceMapLoader() {
  return <DeviceMap />;
}
