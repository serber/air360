const NOMINATIM_URL = "https://nominatim.openstreetmap.org/reverse";
const USER_AGENT = "air360-backend/1.0";

interface NominatimResponse {
  display_name: string;
  address: {
    country?: string;
    country_code?: string;
    city?: string;
    town?: string;
    village?: string;
    municipality?: string;
    [key: string]: string | undefined;
  };
}

export interface GeoAddress {
  geo_country: string | null;
  geo_country_code: string | null;
  geo_city: string | null;
  geo_display: string | null;
}

export async function reverseGeocode(
  latitude: number,
  longitude: number,
): Promise<GeoAddress | null> {
  const url = `${NOMINATIM_URL}?lat=${latitude}&lon=${longitude}&format=json`;

  let response: Response;
  try {
    response = await fetch(url, {
      headers: { "User-Agent": USER_AGENT, Accept: "application/json" },
      signal: AbortSignal.timeout(10_000),
    });
  } catch {
    return null;
  }

  if (!response.ok) return null;

  const data = (await response.json()) as NominatimResponse;
  const addr = data.address;

  return {
    geo_country: addr.country ?? null,
    geo_country_code: addr.country_code?.toUpperCase() ?? null,
    geo_city: addr.city ?? addr.town ?? addr.village ?? addr.municipality ?? null,
    geo_display: data.display_name ?? null,
  };
}
