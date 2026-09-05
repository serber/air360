"use client";

import { useTranslations } from "next-intl";
import { setPortalTheme, usePortalTheme } from "@/lib/theme";

function MoonIcon() {
  return (
    <svg
      aria-hidden="true"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeLinecap="round"
      strokeLinejoin="round"
      strokeWidth="1.6"
    >
      <path d="M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79Z" />
    </svg>
  );
}

function SunIcon() {
  return (
    <svg
      aria-hidden="true"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeLinecap="round"
      strokeWidth="1.6"
    >
      <circle cx="12" cy="12" r="4" />
      <path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M4.93 19.07l1.41-1.41M17.66 6.34l1.41-1.41" />
    </svg>
  );
}

export function PortalThemeToggle() {
  const t = useTranslations("common");
  const theme = usePortalTheme();

  function toggleTheme() {
    setPortalTheme(theme === "dark" ? "light" : "dark");
  }

  return (
    <button
      aria-label={t("themeToggle")}
      className="air-icon-button"
      onClick={toggleTheme}
      title={t("themeToggle")}
      type="button"
    >
      {theme === "dark" ? <SunIcon /> : <MoonIcon />}
    </button>
  );
}
