import { createHash } from "node:crypto";

type City = {
  name: string;
  slug: string;
  latitude: number;
  longitude: number;
  radiusKm: number;
};

type Args = {
  apiBaseUrl: string;
  cityLimit: number | null;
  commit: boolean;
  concurrency: number;
  maxPerCity: number;
  minPerCity: number;
};

type RegisterResponse = {
  public_id: string;
};

const cities: City[] = [
  { name: "London", slug: "london", latitude: 51.5074, longitude: -0.1278, radiusKm: 32 },
  { name: "Paris", slug: "paris", latitude: 48.8566, longitude: 2.3522, radiusKm: 28 },
  { name: "Berlin", slug: "berlin", latitude: 52.52, longitude: 13.405, radiusKm: 30 },
  { name: "Madrid", slug: "madrid", latitude: 40.4168, longitude: -3.7038, radiusKm: 28 },
  { name: "Rome", slug: "rome", latitude: 41.9028, longitude: 12.4964, radiusKm: 26 },
  { name: "Amsterdam", slug: "amsterdam", latitude: 52.3676, longitude: 4.9041, radiusKm: 22 },
  { name: "Brussels", slug: "brussels", latitude: 50.8503, longitude: 4.3517, radiusKm: 18 },
  { name: "Vienna", slug: "vienna", latitude: 48.2082, longitude: 16.3738, radiusKm: 22 },
  { name: "Prague", slug: "prague", latitude: 50.0755, longitude: 14.4378, radiusKm: 22 },
  { name: "Warsaw", slug: "warsaw", latitude: 52.2297, longitude: 21.0122, radiusKm: 26 },
  { name: "Budapest", slug: "budapest", latitude: 47.4979, longitude: 19.0402, radiusKm: 24 },
  { name: "Milan", slug: "milan", latitude: 45.4642, longitude: 9.19, radiusKm: 24 },
  { name: "Munich", slug: "munich", latitude: 48.1351, longitude: 11.582, radiusKm: 22 },
  { name: "Hamburg", slug: "hamburg", latitude: 53.5511, longitude: 9.9937, radiusKm: 25 },
  { name: "Barcelona", slug: "barcelona", latitude: 41.3874, longitude: 2.1686, radiusKm: 24 },
  { name: "Lisbon", slug: "lisbon", latitude: 38.7223, longitude: -9.1393, radiusKm: 22 },
  { name: "Dublin", slug: "dublin", latitude: 53.3498, longitude: -6.2603, radiusKm: 20 },
  { name: "Copenhagen", slug: "copenhagen", latitude: 55.6761, longitude: 12.5683, radiusKm: 20 },
  { name: "Stockholm", slug: "stockholm", latitude: 59.3293, longitude: 18.0686, radiusKm: 26 },
  { name: "Oslo", slug: "oslo", latitude: 59.9139, longitude: 10.7522, radiusKm: 24 },
  { name: "Helsinki", slug: "helsinki", latitude: 60.1699, longitude: 24.9384, radiusKm: 24 },
  { name: "Zurich", slug: "zurich", latitude: 47.3769, longitude: 8.5417, radiusKm: 18 },
  { name: "Geneva", slug: "geneva", latitude: 46.2044, longitude: 6.1432, radiusKm: 18 },
  { name: "Athens", slug: "athens", latitude: 37.9838, longitude: 23.7275, radiusKm: 24 },
  { name: "Istanbul", slug: "istanbul", latitude: 41.0082, longitude: 28.9784, radiusKm: 38 },
  { name: "Bucharest", slug: "bucharest", latitude: 44.4268, longitude: 26.1025, radiusKm: 26 },
  { name: "Sofia", slug: "sofia", latitude: 42.6977, longitude: 23.3219, radiusKm: 22 },
  { name: "Belgrade", slug: "belgrade", latitude: 44.7866, longitude: 20.4489, radiusKm: 22 },
  { name: "Zagreb", slug: "zagreb", latitude: 45.815, longitude: 15.9819, radiusKm: 18 },
  { name: "Ljubljana", slug: "ljubljana", latitude: 46.0569, longitude: 14.5058, radiusKm: 16 },
  { name: "Bratislava", slug: "bratislava", latitude: 48.1486, longitude: 17.1077, radiusKm: 18 },
  { name: "Krakow", slug: "krakow", latitude: 50.0647, longitude: 19.945, radiusKm: 18 },
  { name: "Gdansk", slug: "gdansk", latitude: 54.352, longitude: 18.6466, radiusKm: 18 },
  { name: "Riga", slug: "riga", latitude: 56.9496, longitude: 24.1052, radiusKm: 20 },
  { name: "Tallinn", slug: "tallinn", latitude: 59.437, longitude: 24.7536, radiusKm: 18 },
  { name: "Vilnius", slug: "vilnius", latitude: 54.6872, longitude: 25.2797, radiusKm: 18 },
  { name: "Luxembourg", slug: "luxembourg", latitude: 49.6116, longitude: 6.1319, radiusKm: 14 },
  { name: "Lyon", slug: "lyon", latitude: 45.764, longitude: 4.8357, radiusKm: 20 },
  { name: "Marseille", slug: "marseille", latitude: 43.2965, longitude: 5.3698, radiusKm: 20 },
  { name: "Manchester", slug: "manchester", latitude: 53.4808, longitude: -2.2426, radiusKm: 22 },
];

function parseArgs(): Args {
  const args = new Map<string, string | boolean>();
  for (const item of process.argv.slice(2)) {
    if (item === "--commit") {
      args.set("commit", true);
      continue;
    }

    const [key, value] = item.replace(/^--/, "").split("=", 2);
    if (!key || value === undefined) {
      throw new Error(`Invalid argument: ${item}`);
    }
    args.set(key, value);
  }

  const minPerCity = numberArg(args, "min-per-city", 100);
  const maxPerCity = numberArg(args, "max-per-city", 150);

  if (minPerCity < 1 || maxPerCity < minPerCity) {
    throw new Error("--min-per-city must be >= 1 and <= --max-per-city");
  }

  return {
    apiBaseUrl: stringArg(args, "api-base", "https://api.air360.ru"),
    cityLimit: optionalNumberArg(args, "city-limit"),
    commit: args.get("commit") === true,
    concurrency: numberArg(args, "concurrency", 8),
    maxPerCity,
    minPerCity,
  };
}

async function main(): Promise<void> {
  const args = parseArgs();
  const selectedCities = args.cityLimit ? cities.slice(0, args.cityLimit) : cities;
  const devices = selectedCities.flatMap((city, cityIndex) =>
    buildCityDevices(city, cityIndex, args.minPerCity, args.maxPerCity),
  );

  console.log(
    `${args.commit ? "commit" : "dry-run"}: ${devices.length} devices across ${selectedCities.length} cities`,
  );
  console.log(`target API: ${args.apiBaseUrl}`);

  if (!args.commit) {
    for (const preview of devices.slice(0, 5)) {
      console.log(
        `preview ${preview.deviceId} ${preview.name} ${preview.latitude.toFixed(5)},${preview.longitude.toFixed(5)}`,
      );
    }
    console.log("add --commit to write these devices and one measurement batch per device");
    return;
  }

  let completed = 0;
  await runConcurrent(devices, args.concurrency, async (device) => {
    await seedDevice(args.apiBaseUrl, device);
    completed += 1;
    if (completed % 100 === 0 || completed === devices.length) {
      console.log(`seeded ${completed}/${devices.length}`);
    }
  });
}

type DemoDevice = {
  batchId: number;
  city: City;
  deviceId: number;
  index: number;
  latitude: number;
  longitude: number;
  name: string;
  secret: string;
};

function buildCityDevices(
  city: City,
  cityIndex: number,
  minPerCity: number,
  maxPerCity: number,
): DemoDevice[] {
  const rng = mulberry32(hashNumber(city.slug));
  const count = randomInt(rng, minPerCity, maxPerCity);

  return Array.from({ length: count }, (_, index) => {
    const location = jitterLocation(city, rng);
    const deviceId = 270_000_000_000_000 + cityIndex * 1_000 + index;

    return {
      batchId: 1_900_000_000_000 + cityIndex * 1_000 + index,
      city,
      deviceId,
      index,
      latitude: location.latitude,
      longitude: location.longitude,
      name: `Air360 Demo ${city.name} ${String(index + 1).padStart(3, "0")}`,
      secret: `air360-demo-${city.slug}-${index + 1}`,
    };
  });
}

async function seedDevice(apiBaseUrl: string, device: DemoDevice): Promise<void> {
  const baseUrl = apiBaseUrl.replace(/\/+$/, "");
  const registerUrl = `${baseUrl}/v1/devices/${device.deviceId}/register`;
  const batchUrl = `${baseUrl}/v1/devices/${device.deviceId}/batches/${device.batchId}`;

  await putJson(registerUrl, {
    schema_version: 1,
    name: device.name,
    firmware_version: "demo-map-seed",
    location: {
      latitude: device.latitude,
      longitude: device.longitude,
    },
    upload_secret_hash: hashUploadSecret(device.secret),
  });

  const sentAt = Date.now();
  await putJson(
    batchUrl,
    {
      schema_version: 1,
      sent_at_unix_ms: sentAt,
      device: {
        device_id: String(device.deviceId),
        firmware_version: "demo-map-seed",
      },
      batch: {
        sample_count: 5,
        samples: buildSamples(device, sentAt),
      },
    },
    { Authorization: `Bearer ${device.secret}` },
  );
}

function buildSamples(device: DemoDevice, sampledAt: number) {
  const rng = mulberry32(hashNumber(`${device.city.slug}-${device.index}-measurements`));
  const profile = cityProfile(device.city.slug);
  const temperature = profile.temperature + randomRange(rng, -4, 4);
  const humidity = clamp(profile.humidity + randomRange(rng, -16, 16), 18, 85);
  const pm25 = clamp(profile.pm25 * randomRange(rng, 0.55, 1.9), 1, 120);
  const pm10 = clamp(pm25 * randomRange(rng, 1.4, 2.8), 2, 220);
  const pm1 = clamp(pm25 * randomRange(rng, 0.45, 0.8), 0.5, 80);
  const pm4 = clamp(pm25 * randomRange(rng, 1.05, 1.7), 1, 150);
  const co2 = clamp(profile.co2 + randomRange(rng, -180, 850), 390, 2600);

  return [
    {
      sensor_type: "bme280",
      sample_time_unix_ms: sampledAt,
      values: [
        { kind: "temperature_c", value: round(temperature, 1) },
        { kind: "humidity_percent", value: round(humidity, 1) },
        { kind: "pressure_hpa", value: round(1013 + randomRange(rng, -18, 18), 1) },
      ],
    },
    {
      sensor_type: "scd30",
      sample_time_unix_ms: sampledAt,
      values: [{ kind: "co2_ppm", value: round(co2, 0) }],
    },
    {
      sensor_type: "sps30",
      sample_time_unix_ms: sampledAt,
      values: [
        { kind: "pm1_0_ug_m3", value: round(pm1, 1) },
        { kind: "pm2_5_ug_m3", value: round(pm25, 1) },
        { kind: "pm4_0_ug_m3", value: round(pm4, 1) },
        { kind: "pm10_0_ug_m3", value: round(pm10, 1) },
        { kind: "typical_particle_size_um", value: round(randomRange(rng, 0.35, 1.35), 2) },
      ],
    },
    {
      sensor_type: "veml7700",
      sample_time_unix_ms: sampledAt,
      values: [{ kind: "illuminance_lux", value: round(randomRange(rng, 80, 8200), 0) }],
    },
    {
      sensor_type: "gps_nmea",
      sample_time_unix_ms: sampledAt,
      values: [
        { kind: "latitude_deg", value: round(device.latitude, 6) },
        { kind: "longitude_deg", value: round(device.longitude, 6) },
      ],
    },
  ];
}

async function putJson(
  url: string,
  body: unknown,
  headers: Record<string, string> = {},
): Promise<void> {
  const payload = JSON.stringify(body);
  let lastError: unknown;

  for (let attempt = 1; attempt <= 5; attempt += 1) {
    try {
      const response = await fetch(url, {
        method: "PUT",
        headers: {
          "Content-Type": "application/json",
          ...headers,
        },
        body: payload,
      });

      if (response.ok) {
        return;
      }

      const text = await response.text();
      lastError = new Error(`HTTP ${response.status}: ${text}`);

      if (response.status < 500) {
        throw lastError;
      }
    } catch (error) {
      lastError = error;
    }

    await sleep(250 * attempt);
  }

  throw new Error(`PUT ${url} failed after retries: ${String(lastError)}`);
}

function hashUploadSecret(secret: string): string {
  return `sha256:${createHash("sha256").update(secret).digest("base64url")}`;
}

function cityProfile(slug: string) {
  const polluted = new Set(["london", "paris", "milan", "warsaw", "krakow", "istanbul", "bucharest"]);
  const cool = new Set(["oslo", "stockholm", "helsinki", "tallinn", "riga", "vilnius"]);
  const warm = new Set(["madrid", "rome", "barcelona", "lisbon", "athens", "marseille"]);

  return {
    co2: polluted.has(slug) ? 760 : 590,
    humidity: warm.has(slug) ? 44 : cool.has(slug) ? 62 : 54,
    pm25: polluted.has(slug) ? 28 : 12,
    temperature: warm.has(slug) ? 27 : cool.has(slug) ? 13 : 21,
  };
}

function jitterLocation(city: City, rng: () => number) {
  const distanceKm = city.radiusKm * Math.sqrt(rng());
  const angle = 2 * Math.PI * rng();
  const latitudeOffset = (distanceKm * Math.cos(angle)) / 111.32;
  const longitudeOffset =
    (distanceKm * Math.sin(angle)) /
    (111.32 * Math.cos((city.latitude * Math.PI) / 180));

  return {
    latitude: city.latitude + latitudeOffset,
    longitude: city.longitude + longitudeOffset,
  };
}

async function runConcurrent<T>(
  items: T[],
  concurrency: number,
  worker: (item: T) => Promise<void>,
): Promise<void> {
  let nextIndex = 0;
  const workers = Array.from({ length: Math.max(1, concurrency) }, async () => {
    while (nextIndex < items.length) {
      const item = items[nextIndex];
      nextIndex += 1;
      if (item !== undefined) {
        await worker(item);
      }
    }
  });

  await Promise.all(workers);
}

function stringArg(args: Map<string, string | boolean>, name: string, fallback: string): string {
  const value = args.get(name);
  return typeof value === "string" ? value : fallback;
}

function numberArg(args: Map<string, string | boolean>, name: string, fallback: number): number {
  const value = optionalNumberArg(args, name);
  return value ?? fallback;
}

function optionalNumberArg(args: Map<string, string | boolean>, name: string): number | null {
  const value = args.get(name);
  if (typeof value !== "string") return null;
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) {
    throw new Error(`--${name} must be a number`);
  }
  return parsed;
}

function mulberry32(seed: number): () => number {
  let state = seed;
  return () => {
    state |= 0;
    state = (state + 0x6d2b79f5) | 0;
    let t = Math.imul(state ^ (state >>> 15), 1 | state);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function hashNumber(value: string): number {
  return createHash("sha256").update(value).digest().readUInt32LE(0);
}

function randomInt(rng: () => number, min: number, max: number): number {
  return Math.floor(randomRange(rng, min, max + 1));
}

function randomRange(rng: () => number, min: number, max: number): number {
  return min + (max - min) * rng();
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function round(value: number, digits: number): number {
  const factor = 10 ** digits;
  return Math.round(value * factor) / factor;
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
