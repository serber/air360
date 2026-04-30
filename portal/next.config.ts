import path from "node:path";
import { fileURLToPath } from "node:url";
import type { NextConfig } from "next";

const portalRoot = path.dirname(fileURLToPath(import.meta.url));

const nextConfig: NextConfig = {
  outputFileTracingRoot: portalRoot,
  turbopack: {
    root: portalRoot,
  },
};

export default nextConfig;
