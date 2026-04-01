# Air360 Portal Ubuntu Deployment Guide

## Status

Working deployment guide for the first standalone Air360 portal application on a single Ubuntu server.

This guide assumes:

- no cloud platform
- one plain Ubuntu machine
- the portal is deployed as a separate application from `backend/`
- the current portal is a `Next.js` web application with one public landing page and room for future account screens

## Goal

Deploy the Air360 portal with:

- `systemd` for process supervision
- `nginx` as reverse proxy and TLS terminator
- `ufw` as the host firewall
- `Node.js` LTS for the application runtime

At the current stage, the portal does not require application-specific secrets or database access on the host.
When backend integration starts, environment variables for API endpoints and auth settings should be added to the portal environment file.

## Recommended Host Shape

- OS: Ubuntu `24.04 LTS`
- App runtime: `Node.js 24 LTS`
- Reverse proxy: `nginx`
- Process manager: `systemd`
- Firewall: `ufw`
- TLS: `certbot` with Let's Encrypt

Recommended network model:

- `nginx` listens on ports `80` and `443`
- the portal app listens only on `127.0.0.1:3000`
- the backend API remains a separate service, ideally on its own local port or host

## Why This Shape

- `systemd` is enough for a single Ubuntu host and avoids adding extra process managers such as `pm2`
- `nginx` provides a simple and well-understood TLS and reverse proxy layer
- keeping the Next.js process on localhost reduces accidental exposure
- using the same operational shape as the backend makes deployment and support simpler

## Packages To Install

Install the base system packages:

```bash
sudo apt update
sudo apt install -y nginx ufw git curl unzip
```

## Firewall Setup

Open SSH and web traffic, then enable `ufw`:

```bash
sudo ufw allow OpenSSH
sudo ufw allow 'Nginx Full'
sudo ufw enable
sudo ufw status
```

## Node.js Installation

Do not rely on the default Ubuntu `nodejs` package for this portal unless its version is intentionally acceptable for the project.

As of April 1, 2026:

- `Node.js 24` is the recommended LTS line for a new deployment
- `Next.js 16` requires at least `Node.js 20.9`

Use the official Linux binary for the active LTS branch:

```bash
cd /tmp
curl -fsSLO https://nodejs.org/dist/latest-v24.x/node-v24.14.1-linux-x64.tar.xz
sudo mkdir -p /opt/node
sudo tar -xJf node-v24.14.1-linux-x64.tar.xz -C /opt/node
sudo ln -sf /opt/node/node-v24.14.1-linux-x64/bin/node /usr/local/bin/node
sudo ln -sf /opt/node/node-v24.14.1-linux-x64/bin/npm /usr/local/bin/npm
sudo ln -sf /opt/node/node-v24.14.1-linux-x64/bin/npx /usr/local/bin/npx
node -v
npm -v
```

Before installing, re-check the current active LTS branch on the official Node.js release page if this document is no longer recent.

## Service User And Directory Layout

Create a dedicated system user and deployment directory:

```bash
sudo adduser --system --group --home /opt/air360 air360
sudo mkdir -p /opt/air360/app
sudo chown -R air360:air360 /opt/air360
```

Suggested layout:

- `/opt/air360` for the Air360 repository checkout
- `/opt/air360/portal` for the portal application itself
- `/etc/air360-portal.env` for environment variables
- `/etc/systemd/system/air360-portal.service` for the service unit
- `/etc/nginx/sites-available/air360-portal` for the nginx site config

## Deploy The Portal Code

Clone the Air360 repository as the service user:

```bash
sudo -u air360 git clone <your-repo-url> /opt/air360/app
```

If the repository was cloned or unpacked as `root`, fix ownership before installing dependencies:

```bash
sudo chown -R air360:air360 /opt/air360/app
```

Install dependencies and build:

```bash
cd /opt/air360/portal
sudo -u air360 npm ci
sudo -u air360 npm run build
```

If you hit a restricted CI or sandbox environment where Turbopack worker processes are blocked, use this build fallback instead:

```bash
cd /opt/air360/portal
sudo -u air360 npx next build --webpack
```

For a normal Ubuntu host, `npm run build` should remain the default command.

## Environment File

Create `/etc/air360-portal.env`:

```dotenv
NODE_ENV=production
PORT=3001
NEXT_TELEMETRY_DISABLED=1
```

Restrict permissions:

```bash
sudo chown root:air360 /etc/air360-portal.env
sudo chmod 640 /etc/air360-portal.env
```

Later, this file can also contain portal-specific runtime settings such as backend API base URLs.

## systemd Service

Create `/etc/systemd/system/air360-portal.service`:

```ini
[Unit]
Description=Air360 Portal
After=network.target

[Service]
Type=simple
User=air360
Group=air360
WorkingDirectory=/opt/air360/portal
EnvironmentFile=/etc/air360-portal.env
ExecStart=/usr/local/bin/npm run start -- --hostname 127.0.0.1 --port 3000
Restart=always
RestartSec=3
KillSignal=SIGINT
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

Enable and start the service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable air360-portal
sudo systemctl start air360-portal
sudo systemctl status air360-portal
```

Live logs:

```bash
journalctl -u air360-portal -f
```

## nginx Reverse Proxy

Create `/etc/nginx/sites-available/air360-portal`:

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

Replace `portal.example.com` with the real hostname of the portal.

Enable the site:

```bash
sudo ln -s /etc/nginx/sites-available/air360-portal /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

## TLS With Let's Encrypt

Once DNS for the portal hostname points to the server, install Certbot and issue the certificate:

```bash
sudo snap install --classic certbot
sudo ln -s /snap/bin/certbot /usr/bin/certbot
sudo certbot --nginx -d portal.example.com
sudo certbot renew --dry-run
```

Replace `portal.example.com` with the real hostname before issuing the certificate.

## First Runtime Checks

Once the service is running:

- confirm `nginx` is healthy
- confirm `air360-portal.service` is healthy
- request the portal over HTTP or HTTPS through `nginx`
- verify that the Next.js process is not exposed directly on a public port

Example checks:

```bash
systemctl status nginx
systemctl status air360-portal
curl -I http://127.0.0.1:3000
curl -I https://air360.ru
ss -ltnp | grep 3000
```

## Update Deployment

When new portal changes are deployed:

```bash
cd /opt/air360
sudo -u air360 git pull
cd /opt/air360/portal
sudo -u air360 npm ci
sudo -u air360 npm run build
sudo systemctl restart air360-portal
```

If the deployed environment requires the webpack fallback:

```bash
cd /opt/air360/app
sudo -u air360 git pull
cd /opt/air360/portal
sudo -u air360 npm ci
sudo -u air360 npx next build --webpack
sudo systemctl restart air360-portal
```

## Notes

- the portal is a separate application from `backend/`, even if both are deployed on the same Ubuntu host
- at the current stage, no portal-specific secrets are required on the server
- once backend integration starts, document required API base URLs and auth-related variables in `/etc/air360-portal.env`
