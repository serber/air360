import type { KindMeasurements } from "@/lib/api";
import { kindLabel, sensorLabel } from "@/lib/api";

/**
 * Declarative metadata describing which measurement kinds are drawn together on a
 * single chart.
 *
 * Two sensors reporting the *same* kind already merge on their own — the backend
 * groups `by_kind`, so each sensor becomes one series inside that kind's chart
 * (this is why humidity from a BME280 and an SHT4X shares a chart). Chart groups
 * extend that to *different* kinds that describe one physical quantity: the PM
 * mass fractions, the number-concentration bins, the particle-count bins, and the
 * two pressure references. Every kind inside a group must share a unit so a single
 * Y axis stays meaningful.
 *
 * To combine another set of kinds, add an entry here and a matching title under
 * the `chartGroups` i18n namespace — no render code changes are needed.
 */
export type ChartGroup = {
  /** Stable id; used as the React key for the merged chart card. */
  id: string;
  /** Key under the `chartGroups` i18n namespace for the card title. */
  titleKey: string;
  /** Member kinds in the order their series should appear. All share one unit. */
  kinds: readonly string[];
};

export const CHART_GROUPS: readonly ChartGroup[] = [
  {
    id: "pressure",
    titleKey: "pressure",
    kinds: ["pressure_hpa", "pressure_hpa_raw"],
  },
  {
    id: "particulate_mass",
    titleKey: "particulateMass",
    kinds: [
      "pm1_0_ug_m3",
      "pm2_5_ug_m3",
      "pm4_0_ug_m3",
      "pm10_0_ug_m3",
    ],
  },
  {
    id: "number_concentration",
    titleKey: "numberConcentration",
    kinds: [
      "nc0_5_per_cm3",
      "nc1_0_per_cm3",
      "nc2_5_per_cm3",
      "nc4_0_per_cm3",
      "nc10_0_per_cm3",
    ],
  },
  {
    id: "particle_counts",
    titleKey: "particleCounts",
    kinds: [
      "pc0_3_per_0_1l",
      "pc0_5_per_0_1l",
      "pc1_0_per_0_1l",
      "pc2_5_per_0_1l",
      "pc5_0_per_0_1l",
      "pc10_per_0_1l",
    ],
  },
];

export type GroupedSeries = KindMeasurements["series"][number] & {
  kind: string;
  label: string;
};

export type GroupedChart = {
  /** A real member kind — carries the shared unit for the Y axis and tooltips. */
  kind: string;
  title: string;
  series: GroupedSeries[];
};

/**
 * Builds the series for a chart that merges several kinds. When every point comes
 * from a single sensor the sensor suffix is dropped (`PM2.5`, not `PM2.5 · SPS30`);
 * with two or more sensors it is kept so overlapping kinds stay distinguishable.
 */
export function buildGroupedChart(
  group: ChartGroup,
  members: KindMeasurements[],
  title: string,
): GroupedChart {
  const sensorTypes = new Set(
    members.flatMap((member) => member.series.map((series) => series.sensor_type)),
  );
  const showSensor = sensorTypes.size > 1;

  const series: GroupedSeries[] = members.flatMap((member) =>
    member.series.map((item) => ({
      ...item,
      kind: member.kind,
      label: showSensor
        ? `${kindLabel(member.kind)} · ${sensorLabel(item.sensor_type)}`
        : kindLabel(member.kind),
    })),
  );

  return {
    kind: members[0]?.kind ?? group.kinds[0],
    series,
    title,
  };
}
