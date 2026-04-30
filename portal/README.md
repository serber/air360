# Air360 Portal

This directory contains the Air360 public portal application.

The portal currently provides:

- `Next.js` 16 with the `App Router`
- `React` 19 and `TypeScript`
- `Tailwind CSS` 4
- `ESLint` 9
- a public device map at `/`
- public device measurement charts at `/devices/:public_id`

## Development

Install dependencies and start the local dev server:

```bash
npm install
npm run dev
```

Open [http://localhost:3000](http://localhost:3000) with your browser to see the result.

By default the browser fetches the backend API from the same origin under
`/v1`. For a separate backend host, set:

```bash
NEXT_PUBLIC_AIR360_API_BASE_URL=http://localhost:3000
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

## Notes

- Minimum Node.js version: `20.9.0`
- Package manager: `npm`
- Main application entry: `src/app/page.tsx`
- Global metadata and layout: `src/app/layout.tsx`
