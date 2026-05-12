import type { DeviceSummary } from "@/lib/api";
import { useFormatter, useTranslations } from "next-intl";
import {
  countryCodeFlag,
  formatValue,
  isDeviceStale,
  kindLabel,
} from "@/lib/api";

type DevicePopupProps = {
  device: DeviceSummary;
  onClose?: () => void;
};

export function DevicePopup({ device, onClose }: DevicePopupProps) {
  const t = useTranslations("devicePopup");
  const format = useFormatter();
  const isStale = isDeviceStale(device.last_seen_at);
  const flag = countryCodeFlag(device.geo_country_code);
  const readings = device.sensors.flatMap((sensor) =>
    sensor.readings.map((reading) => ({
      key: `${sensor.sensor_type}-${reading.kind}`,
      label: kindLabel(reading.kind),
      value: formatValue(reading.kind, reading.value),
    })),
  );

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
              date: format.dateTime(new Date(device.last_seen_at), {
                dateStyle: "medium",
                timeStyle: "short",
              }),
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
        ) : readings.length === 0 ? (
          <p className="air-device-popup-note">{t("noMeasurements")}</p>
        ) : (
          readings.slice(0, 9).map((reading) => (
            <div className="air-device-popup-kv" key={reading.key}>
              <div className="air-device-popup-kv-label">{reading.label}</div>
              <div className="air-device-popup-kv-value">{reading.value}</div>
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
