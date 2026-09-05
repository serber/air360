# Air360 Portal

This directory contains the Air360 public portal application.

The portal currently provides:

- `Next.js` 16 with the `App Router`
- `React` 19 and `TypeScript`
- `Tailwind CSS` 4
- `ESLint` 9
- a public home page at `/` using the shared Air360 visual system
- a public device map at `/map` with Air360 layer controls and preserved
  metric legends; marker and cluster values are rounded to the nearest tenth
- a public build guide at `/build` with shield-board and direct ESP32-S3
  wiring paths, a sensor selection guide, and a "Where your measurements go"
  section covering the upload backends the firmware supports
- a privacy policy at `/privacy` with the shared Air360 document layout
- public device measurement charts at `/devices/:public_id` with Air360
  current-reading, period, chart, and metadata panels

## Development

Install dependencies and start the local dev server:

```bash
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) with your browser to see the result.

By default the browser fetches API routes from the portal origin under `/v1`.
Next.js proxies those requests to `https://api.air360.ru/v1`, so local
development does not hit browser CORS restrictions.

To override the server-side API proxy target for local development or staging,
set:

```bash
AIR360_API_BASE_URL=https://api.air360.ru
```

The contact address shown on `/privacy` comes from `CONTACT_EMAIL`
(`src/lib/config.ts`). Pages are dynamically rendered, so it is read from the
server environment at request time; changing it only requires a server restart.

```bash
CONTACT_EMAIL=contact@example.com
```

## Production

Build and start the production server:

```bash
npm run build
npm run start
```

## Documentation

Portal documentation is stored in `../docs/portal/`.

Ubuntu setup and deployment notes are in `../docs/portal/ubuntu-deployment.md`.
Portal-local AI agent guidance is in `CLAUDE.md`.

## Notes

- Minimum Node.js version: `20.9.0`
- Package manager: `npm`
- Main application entry: `src/app/page.tsx`
- Global metadata and layout: `src/app/layout.tsx`
- Shared portal shell and reusable navigation primitives:
  `src/components/PortalShell.tsx`
- API types and fetch helpers: `src/lib/api.ts`
- Map metric color scales shared by the device map and the home preview: `src/lib/map-scales.ts`
- API proxy: `next.config.ts`
