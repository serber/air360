"use client";

import { useSyncExternalStore } from "react";

export type PortalTheme = "light" | "dark";

const STORAGE_KEY = "air360-theme";
const CHANGE_EVENT = "air360-theme-change";

function subscribe(callback: () => void) {
  window.addEventListener("storage", callback);
  window.addEventListener(CHANGE_EVENT, callback);

  return () => {
    window.removeEventListener("storage", callback);
    window.removeEventListener(CHANGE_EVENT, callback);
  };
}

function getSnapshot(): PortalTheme {
  return window.localStorage.getItem(STORAGE_KEY) === "dark" ? "dark" : "light";
}

function getServerSnapshot(): PortalTheme {
  return "light";
}

export function usePortalTheme(): PortalTheme {
  return useSyncExternalStore(subscribe, getSnapshot, getServerSnapshot);
}

export function setPortalTheme(theme: PortalTheme): void {
  document.documentElement.dataset.theme = theme;
  window.localStorage.setItem(STORAGE_KEY, theme);
  window.dispatchEvent(new Event(CHANGE_EVENT));
}
