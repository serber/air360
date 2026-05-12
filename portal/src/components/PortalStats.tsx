"use client";

import { useEffect, useState } from "react";
import { fetchJson, type PortalStatsResponse } from "@/lib/api";

type StatItem = {
  key: keyof PortalStatsResponse;
  label: string;
  format: (value: number) => string;
};

const numberFormatter = new Intl.NumberFormat(undefined);
const compactFormatter = new Intl.NumberFormat(undefined, {
  maximumFractionDigits: 1,
  notation: "compact",
});

const stats: StatItem[] = [
  {
    key: "active_devices",
    label: "Active devices",
    format: (value) => numberFormatter.format(value),
  },
  {
    key: "countries",
    label: "Countries",
    format: (value) => numberFormatter.format(value),
  },
  {
    key: "data_points_24h",
    label: "Data points / 24h",
    format: (value) => compactFormatter.format(value),
  },
];

export function PortalStats() {
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
          <span className="air-kv-label">{stat.label}</span>
          <span className="air-kv-value">
            {data ? stat.format(data[stat.key]) : "-"}
          </span>
        </div>
      ))}
    </div>
  );
}
