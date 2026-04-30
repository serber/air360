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
      className="flex flex-wrap gap-2"
      role="radiogroup"
      aria-label="Measurement period"
    >
      {PERIOD_OPTIONS.map((period) => (
        <button
          aria-checked={period.value === value}
          className="rounded-md border border-slate-300 bg-white px-3 py-2 text-sm font-semibold text-slate-700 transition hover:border-slate-950 hover:text-slate-950 aria-checked:border-slate-950 aria-checked:bg-slate-950 aria-checked:text-white"
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
