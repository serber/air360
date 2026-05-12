import type { DeviceSummary } from "@/lib/api";
import { useLocale, useTranslations } from "next-intl";
import {
  countryCodeFlag,
  formatValue,
  isDeviceStale,
  kindLabel,
  sensorLabel,
} from "@/lib/api";

type DevicePopupProps = {
  device: DeviceSummary;
  onClose?: () => void;
};

const GPS_SENSOR_TYPE = "gps_nmea";
const GPS_READING_KINDS = new Set([
  "latitude_deg",
  "longitude_deg",
  "altitude_m",
  "satellites",
  "speed_knots",
  "course_deg",
  "hdop",
]);

export function DevicePopup({ device, onClose }: DevicePopupProps) {
  const t = useTranslations("devicePopup");
  const locale = useLocale();
  const isStale = isDeviceStale(device.last_seen_at);
  const flag = countryCodeFlag(device.geo_country_code);
  const sensorGroups = device.sensors
    .filter((sensor) => sensor.sensor_type !== GPS_SENSOR_TYPE)
    .map((sensor) => ({
      key: sensor.sensor_type,
      label: sensorLabel(sensor.sensor_type),
      readings: sensor.readings
        .filter((reading) => !GPS_READING_KINDS.has(reading.kind))
        .map((reading) => ({
          key: `${sensor.sensor_type}-${reading.kind}`,
          label: kindLabel(reading.kind),
          value: formatValue(reading.kind, reading.value),
        })),
    }))
    .filter((sensor) => sensor.readings.length > 0);

  return (
    <div className="air-device-popup">
      <div className="air-device-popup-head">
        <div>
          <h2 className="air-device-popup-name">
            {device.name}
            {flag ? <span className="air-device-popup-flag">{flag}</span> : null}
          </h2>
          <div className={isStale ? "air-device-popup-id air-stale" : "air-device-popup-id"}>
            {t("lastSeen", {
              date: formatCompactPopupDate(device.last_seen_at, locale),
            })}
          </div>
        </div>
        {onClose ? (
          <button
            aria-label={t("close")}
            className="air-device-popup-close"
            onClick={onClose}
            type="button"
          >
            <svg
              aria-hidden="true"
              viewBox="0 0 12 12"
              fill="none"
              stroke="currentColor"
              strokeWidth="1.5"
            >
              <path d="M2 2l8 8M10 2l-8 8" />
            </svg>
          </button>
        ) : null}
      </div>

      <div className="air-device-popup-body">
        {isStale ? (
          <p className="air-device-popup-note">
            {t("stale")}
          </p>
        ) : sensorGroups.length === 0 ? (
          <p className="air-device-popup-note">{t("noMeasurements")}</p>
        ) : (
          sensorGroups.map((sensor) => (
            <div className="air-device-popup-sensor" key={sensor.key}>
              <div className="air-device-popup-sensor-name">
                {sensor.label}
              </div>
              <div className="air-device-popup-readings">
                {sensor.readings.map((reading) => (
                  <div className="air-device-popup-kv" key={reading.key}>
                    <div className="air-device-popup-kv-label">{reading.label}</div>
                    <div className="air-device-popup-kv-value">{reading.value}</div>
                  </div>
                ))}
              </div>
            </div>
          ))
        )}
      </div>

      <div className="air-device-popup-foot">
        <a href={`/devices/${device.public_id}`}>{t("openDevice")}</a>
      </div>
    </div>
  );
}

function formatCompactPopupDate(value: string, locale: string): string {
  const date = new Date(value);

  if (Number.isNaN(date.getTime())) {
    return value;
  }

  const now = new Date();
  const isToday =
    date.getFullYear() === now.getFullYear() &&
    date.getMonth() === now.getMonth() &&
    date.getDate() === now.getDate();

  return new Intl.DateTimeFormat(locale, {
    ...(isToday ? {} : { day: "2-digit", month: "2-digit" }),
    hour: "2-digit",
    minute: "2-digit",
  }).format(date);
}
