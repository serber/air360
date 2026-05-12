import { cookies, headers } from "next/headers";
import { getRequestConfig } from "next-intl/server";

const locales = ["en", "ru"] as const;
const defaultLocale = "en";
export const defaultTimeZone = "UTC";
const localeCookieName = "air360-locale";

type Locale = (typeof locales)[number];

export default getRequestConfig(async () => {
  const cookieStore = await cookies();
  const headerStore = await headers();
  const locale =
    resolveLocale(cookieStore.get(localeCookieName)?.value) ??
    resolveLocaleFromAcceptLanguage(headerStore.get("accept-language")) ??
    defaultLocale;

  return {
    locale,
    messages: (await import(`../../messages/${locale}.json`)).default,
    timeZone: defaultTimeZone,
  };
});

function resolveLocale(value: string | undefined): Locale | null {
  return locales.includes(value as Locale) ? (value as Locale) : null;
}

function resolveLocaleFromAcceptLanguage(value: string | null): Locale | null {
  if (!value) {
    return null;
  }

  const requestedLocales = value
    .split(",")
    .map((part) => part.trim().split(";")[0]?.toLowerCase())
    .filter(Boolean);

  for (const requestedLocale of requestedLocales) {
    const baseLocale = requestedLocale.split("-")[0];
    const locale = resolveLocale(baseLocale);

    if (locale) {
      return locale;
    }
  }

  return null;
}
