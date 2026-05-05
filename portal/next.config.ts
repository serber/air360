import path from "node:path";
import { fileURLToPath } from "node:url";
import type { NextConfig } from "next";

const portalRoot = path.dirname(fileURLToPath(import.meta.url));
const apiBaseUrl = (process.env.AIR360_API_BASE_URL ?? "https://api.air360.ru")
  .replace(/\/+$/, "");

const nextConfig: NextConfig = {
  outputFileTracingRoot: portalRoot,
  turbopack: {
    root: portalRoot,
  },
  async rewrites() {
    return [
      {
        source: "/v1/:path*",
        destination: `${apiBaseUrl}/v1/:path*`,
      },
    ];
  },
};

export default nextConfig;
