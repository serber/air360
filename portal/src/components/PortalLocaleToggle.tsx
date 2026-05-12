"use client";

import { useLocale, useTranslations } from "next-intl";

const COOKIE_NAME = "air360-locale";
const COOKIE_MAX_AGE_SECONDS = 60 * 60 * 24 * 365;

export function PortalLocaleToggle() {
  const locale = useLocale();
  const t = useTranslations("common");
  const nextLocale = locale === "ru" ? "en" : "ru";

  function switchLocale() {
    document.cookie = `${COOKIE_NAME}=${nextLocale}; Path=/; Max-Age=${COOKIE_MAX_AGE_SECONDS}; SameSite=Lax`;
    window.location.reload();
  }

  return (
    <button
      aria-label={t("localeToggle")}
      className="air-icon-button air-locale-button"
      onClick={switchLocale}
      title={t("localeToggle")}
      type="button"
    >
      {nextLocale.toUpperCase()}
    </button>
  );
}
