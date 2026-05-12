"use client";

import type { Period } from "@/lib/api";
import { PERIOD_OPTIONS } from "@/lib/api";

type PeriodSelectorProps = {
  value: Period;
  onChange: (period: Period) => void;
};

export function PeriodSelector({ value, onChange }: PeriodSelectorProps) {
  return (
    <div
      className="air-segmented-control"
      role="radiogroup"
      aria-label="Measurement period"
    >
      {PERIOD_OPTIONS.map((period) => (
        <button
          aria-checked={period.value === value}
          key={period.value}
          onClick={() => onChange(period.value)}
          role="radio"
          type="button"
        >
          {period.label}
        </button>
      ))}
    </div>
  );
}
