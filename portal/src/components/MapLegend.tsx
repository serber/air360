"use client";

import type { CSSProperties } from "react";
import { useTranslations } from "next-intl";
import {
  NO_DATA_COLORS,
  scaleGradient,
  type ColorStop,
  type MetricScale,
} from "@/lib/map-scales";

export type FreshnessLevel =
  | "fresh"
  | "hour-day"
  | "day-week"
  | "week-month"
  | "month-plus"
  | "unknown";

const FRESHNESS_CHIPS: readonly { level: FreshnessLevel; label: string }[] = [
  { level: "fresh", label: "< 1h" },
  { level: "hour-day", label: "1h-1d" },
  { level: "day-week", label: "1d-7d" },
  { level: "week-month", label: "7d-30d" },
  { level: "month-plus", label: "> 30d" },
];

function dotStyle(colors: { color: string; ring: string }): CSSProperties {
  return {
    "--marker-bg": colors.color,
    "--marker-ring": colors.ring,
  } as CSSProperties;
}

function stopLabel(stop: ColorStop, t: ReturnType<typeof useTranslations>) {
  if (stop.labelKey) {
    return (
      <span>
        {t(stop.labelKey)}
        {stop.range ? <span className="air-map-legend-range">{stop.range}</span> : null}
      </span>
    );
  }

  return stop.label ?? "";
}

/**
 * Legend for the active map metric: title, gradient bar, one dot per color
 * band, the no-data marker, and the freshness chips shared by every metric.
 */
export function MapLegend({ scale }: { scale: MetricScale }) {
  const t = useTranslations("mapPage.legend");

  return (
    <div className="air-map-legend-panel">
      <div className="air-map-legend-head">
        <span className="air-map-legend-title">
          {scale.title ?? (scale.titleKey ? t(scale.titleKey) : "")}
        </span>
        <span className="air-map-legend-hint">{t(scale.hintKey)}</span>
      </div>
      <div
        aria-hidden="true"
        className="air-map-legend-bar"
        style={{ background: scaleGradient(scale) }}
      />
      <div
        className="air-map-legend-grid"
        style={{ "--legend-columns": scale.columns } as CSSProperties}
      >
        {scale.stops.map((stop) => (
          <span className="air-map-legend-item" key={stop.label ?? stop.labelKey}>
            <span className="air360-legend-dot" style={dotStyle(stop)} />
            {stopLabel(stop, t)}
          </span>
        ))}
      </div>
      {scale.footnote ? (
        <div className="air-map-legend-note">
          <div className="air-map-legend-unit">{scale.footnote.unit}</div>
          {t(scale.footnote.noteKey)}
        </div>
      ) : null}
      <div className="air-map-legend-note">
        <span className="air-map-legend-item">
          <span className="air360-legend-dot" style={dotStyle(NO_DATA_COLORS)} />
          {t("noData")}
        </span>
      </div>
      <div className="air-map-legend-freshness">
        {FRESHNESS_CHIPS.map((chip) => (
          <span
            className={`air360-freshness-chip air360-fresh-${chip.level}`}
            key={chip.level}
          >
            {chip.label}
          </span>
        ))}
      </div>
    </div>
  );
}
