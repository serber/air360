"use client";

import { useTranslations } from "next-intl";
import type { Period } from "@/lib/api";

const PERIOD_OPTIONS: Period[] = ["1h", "24h", "7d", "30d", "90d", "180d", "365d"];

type PeriodSelectorProps = {
  value: Period;
  onChange: (period: Period) => void;
};

export function PeriodSelector({ value, onChange }: PeriodSelectorProps) {
  const t = useTranslations("periods");

  return (
    <div
      className="air-segmented-control"
      role="radiogroup"
      aria-label={t("aria")}
    >
      {PERIOD_OPTIONS.map((period) => (
        <button
          aria-checked={period === value}
          key={period}
          onClick={() => onChange(period)}
          role="radio"
          type="button"
        >
          {t(period)}
        </button>
      ))}
    </div>
  );
}
