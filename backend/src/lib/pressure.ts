// International barometric formula (ISA standard atmosphere)
export function toSeaLevelPressure(stationPressureHpa: number, altitudeM: number): number {
  return stationPressureHpa * Math.pow(1 - altitudeM / 44_330, -5.255);
}
