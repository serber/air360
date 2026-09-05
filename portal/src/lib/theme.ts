"use client";

import { useSyncExternalStore } from "react";

export type PortalTheme = "light" | "dark";

export const THEME_STORAGE_KEY = "air360-theme";
const CHANGE_EVENT = "air360-theme-change";

/**
 * Inline `<script>` body for the document head. It runs before first paint so
 * the stored (or system) theme is on `<html>` before any styles apply, which
 * is what prevents the light-theme flash on a full page load. Keep in sync
 * with `readTheme()` below.
 */
export const THEME_BOOT_SCRIPT = `(function(){try{var t=localStorage.getItem(${JSON.stringify(
  THEME_STORAGE_KEY,
)});if(t!=="dark"&&t!=="light"){t=window.matchMedia("(prefers-color-scheme: dark)").matches?"dark":"light"}document.documentElement.dataset.theme=t}catch(e){}})()`;

function applyTheme(theme: PortalTheme): void {
  document.documentElement.dataset.theme = theme;
}

function readTheme(): PortalTheme {
  try {
    const stored = window.localStorage.getItem(THEME_STORAGE_KEY);
    if (stored === "dark" || stored === "light") {
      return stored;
    }
  } catch {
    // localStorage can be unavailable (private mode, blocked storage).
  }

  return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}

function subscribe(callback: () => void) {
  // Another tab changed the theme: mirror it on this document before notifying.
  const onStorage = (event: StorageEvent) => {
    if (event.key === null || event.key === THEME_STORAGE_KEY) {
      applyTheme(readTheme());
      callback();
    }
  };

  window.addEventListener("storage", onStorage);
  window.addEventListener(CHANGE_EVENT, callback);

  return () => {
    window.removeEventListener("storage", onStorage);
    window.removeEventListener(CHANGE_EVENT, callback);
  };
}

function getServerSnapshot(): PortalTheme {
  return "light";
}

export function usePortalTheme(): PortalTheme {
  return useSyncExternalStore(subscribe, readTheme, getServerSnapshot);
}

export function setPortalTheme(theme: PortalTheme): void {
  applyTheme(theme);
  try {
    window.localStorage.setItem(THEME_STORAGE_KEY, theme);
  } catch {
    // Still apply the theme for this page even if it cannot be persisted.
  }
  window.dispatchEvent(new Event(CHANGE_EVENT));
}
