<!-- BEGIN:nextjs-agent-rules -->
# This is NOT the Next.js you know

This version has breaking changes — APIs, conventions, and file structure may all differ from your training data. Read the relevant guide in `node_modules/next/dist/docs/` before writing any code. Heed deprecation notices.
<!-- END:nextjs-agent-rules -->

# Air360 portal guidance

## Scope

- This file is the portal-local working contract for AI agents operating inside `portal/`.
- `portal/` is the source of truth for implemented public portal behavior.
- `../docs/portal/` explains portal scope, deployment, and ADRs.
- `../backend/` is the source of truth for the API consumed by portal pages.

## Commands

Run commands from this directory.

```bash
npm install
npm run dev
npm run build
npm run lint
npm start
```

Local `/v1/*` requests are proxied by `next.config.ts` to `AIR360_API_BASE_URL`, defaulting to `https://api.air360.ru`.

## Read first

| If the task is about... | Read first |
|-------------------------|------------|
| Portal docs and scope | `../docs/portal/README.md` |
| Map and device pages | `../docs/portal/adr/portal-map-and-device-pages-adr.md` |
| Backend API consumed by the portal | `../docs/backend/README.md` |
| Deployment | `../docs/portal/ubuntu-deployment.md` |

## First code files to inspect

- App routes: `src/app/page.tsx`, `src/app/map/page.tsx`, `src/app/devices/[public_id]/page.tsx`, `src/app/build/page.tsx`, `src/app/privacy/page.tsx`
- Layout and global CSS: `src/app/layout.tsx`, `src/app/globals.css`
- API contracts and formatting: `src/lib/api.ts`, `src/lib/config.ts`
- Map UI: `src/components/DeviceMap.tsx`, `src/components/DeviceMapLoader.tsx`, `src/components/DevicePopup.tsx`
- Device detail UI: `src/components/DeviceDetail.tsx`, `src/components/SensorChart.tsx`, `src/components/PeriodSelector.tsx`
- API proxy: `next.config.ts`

## Implemented pages

- `/`
- `/map`
- `/devices/:public_id`
- `/build`
- `/privacy`

## Co-change expectations

- If backend response shapes change, update `src/lib/api.ts`, affected components, `../docs/portal/README.md`, and `../docs/backend/README.md`.
- If routes or visible page behavior change, update `portal/README.md`, `../docs/portal/README.md`, and the relevant portal ADR.
- If proxy or environment behavior changes, update `next.config.ts`, `portal/README.md`, and `../docs/portal/ubuntu-deployment.md`.
- If adding Next.js APIs or changing framework conventions, read the local `node_modules/next/dist/docs/` guide first.
