"use client";

import { useEffect, useState } from "react";
import { useFormatter, useTranslations } from "next-intl";
import { fetchJson, type PortalStatsResponse } from "@/lib/api";

type StatItem = {
  key: keyof PortalStatsResponse;
  labelKey: string;
  format: "number" | "compact";
};

const stats: StatItem[] = [
  {
    key: "active_devices",
    labelKey: "activeDevices",
    format: "number",
  },
  {
    key: "countries",
    labelKey: "countries",
    format: "number",
  },
  {
    key: "data_points_24h",
    labelKey: "dataPoints24h",
    format: "compact",
  },
];

export function PortalStats() {
  const t = useTranslations("stats");
  const format = useFormatter();
  const [data, setData] = useState<PortalStatsResponse | null>(null);

  useEffect(() => {
    const controller = new AbortController();

    fetchJson<PortalStatsResponse>("/v1/stats", controller.signal)
      .then(setData)
      .catch(() => {
        if (!controller.signal.aborted) {
          setData(null);
        }
      });

    return () => controller.abort();
  }, []);

  return (
    <div className="air-hero-stats">
      {stats.map((stat) => (
        <div className="air-kv" key={stat.key}>
          <span className="air-kv-label">{t(stat.labelKey)}</span>
          <span className="air-kv-value">
            {data
              ? format.number(data[stat.key], {
                  maximumFractionDigits: stat.format === "compact" ? 1 : 0,
                  notation: stat.format === "compact" ? "compact" : "standard",
                })
              : "-"}
          </span>
        </div>
      ))}
    </div>
  );
}
