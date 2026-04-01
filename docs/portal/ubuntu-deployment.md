# Air360 Portal On Ubuntu

## Scope

This document describes how to run and deploy the `portal/` Next.js application on Ubuntu.

The current portal is a standalone frontend application built with:

- `Next.js` 16
- `React` 19
- `TypeScript`
- `npm`

## Requirements

- Ubuntu `22.04 LTS` or newer
- Node.js `20.9.0` or newer
- `npm`
- access to the repository checkout

The official Next.js documentation for the current release states that Next.js 16 requires Node.js `20.9` or newer.

## 1. Install Node.js

Use either `nvm` or a system package source that provides Node.js 20+.

Example with `nvm`:

```bash
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
source ~/.nvm/nvm.sh
nvm install 24
nvm use 24
node -v
npm -v
```

## 2. Install Project Dependencies

From the repository root:

```bash
cd portal
npm ci
```

If `package-lock.json` changes in the future, keep using `npm ci` on servers for reproducible installs.

## 3. Run In Development Mode

```bash
cd portal
npm run dev
```

By default, the development server listens on:

- `http://localhost:3000`

## 4. Build For Production

```bash
cd portal
npm run build
```

This creates the optimized production build in `.next/`.

If you are building inside a restricted sandbox or CI environment where Turbopack worker processes are blocked, use this compatibility fallback:

```bash
cd portal
npx next build --webpack
```

## 5. Run The Production Server

```bash
cd portal
PORT=3000 npm run start
```

The production server binds to the selected `PORT`.

## 6. Run As A systemd Service

Example unit file:

```ini
[Unit]
Description=Air360 Portal
After=network.target

[Service]
Type=simple
User=www-data
Group=www-data
WorkingDirectory=/opt/air360/portal
Environment=NODE_ENV=production
Environment=PORT=3000
ExecStart=/usr/bin/npm run start
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Save it as:

- `/etc/systemd/system/air360-portal.service`

Then enable and start it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable air360-portal
sudo systemctl start air360-portal
sudo systemctl status air360-portal
```

## 7. Put Nginx In Front Of Next.js

Example site config:

```nginx
server {
    listen 80;
    server_name portal.example.com;

    location / {
        proxy_pass http://127.0.0.1:3000;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
```

Typical activation flow:

```bash
sudo ln -s /etc/nginx/sites-available/air360-portal /etc/nginx/sites-enabled/air360-portal
sudo nginx -t
sudo systemctl reload nginx
```

## 8. Update Deployment

When new portal changes are deployed:

```bash
cd /opt/air360/portal
git pull
npm ci
npm run build
sudo systemctl restart air360-portal
```

## Notes

- No application-specific environment variables are required yet.
- Once backend integration starts, document required API base URLs and secrets in this file or a dedicated operations document.
- If HTTPS is needed on Ubuntu, terminate TLS in Nginx and use a certificate managed by `certbot` or your existing infrastructure.
