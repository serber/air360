# Air360 Backend Ubuntu Deployment Guide

## Status

Working deployment guide for the first standalone Air360 API backend on a single Ubuntu server.

This guide assumes:

- no cloud platform
- one plain Ubuntu machine
- the Air360 frontend is a separate application
- this service is only the API backend

## Goal

Deploy the Fastify-based Air360 API backend with:

- `systemd` for process supervision
- `nginx` as reverse proxy and TLS terminator
- `ufw` as the host firewall
- `Node.js` LTS for the application runtime

For the current backend scaffold, no database is required yet.
When backend persistence is added, `PostgreSQL` and `TimescaleDB` are the intended data layer.

## Recommended Host Shape

- OS: Ubuntu `24.04 LTS`
- App runtime: `Node.js 24 LTS`
- Reverse proxy: `nginx`
- Process manager: `systemd`
- Firewall: `ufw`
- TLS: `certbot` with Let's Encrypt

Recommended network model:

- `nginx` listens on ports `80` and `443`
- the Fastify app listens only on `127.0.0.1:3000`
- PostgreSQL, if added later, should also stay private to the host or private network

## Why This Shape

- `systemd` is enough for a single Ubuntu host and avoids adding extra process managers such as `pm2`
- `nginx` provides a simple and well-understood TLS and reverse proxy layer
- keeping the Fastify process on localhost reduces accidental exposure
- this setup is easy to debug, patch, and back up on a non-cloud server

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

Do not rely on the default Ubuntu `nodejs` package for this backend unless its version is intentionally acceptable for the project.

As of March 31, 2026:

- `Node.js 24` is the recommended LTS line for a new deployment
- `Node.js 22` is already in maintenance mode

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
sudo mkdir -p /opt/air360/backend
sudo chown -R air360:air360 /opt/air360
```

Suggested layout:

- `/opt/air360/backend` for the backend source and build output
- `/etc/air360-backend.env` for environment variables
- `/etc/systemd/system/air360-backend.service` for the service unit
- `/etc/nginx/sites-available/air360-backend` for the nginx site config

Ready-to-copy examples for this guide are stored in `docs/backend/`:

- `air360-backend.service`
- `air360-backend.nginx.conf`
- `air360-backend-release-deploy.sh`

## Deploy The Backend Code

Clone the backend repository as the service user:

```bash
sudo -u air360 git clone <your-repo-url> /opt/air360/backend
```

Install dependencies and build:

```bash
cd /opt/air360/backend
sudo -u air360 npm ci
sudo -u air360 npm run build
```

## Environment File

Create `/etc/air360-backend.env`:

```dotenv
HOST=127.0.0.1
PORT=3000
LOG_LEVEL=info
```

Restrict permissions:

```bash
sudo chown root:air360 /etc/air360-backend.env
sudo chmod 640 /etc/air360-backend.env
```

Later, this file can also contain database connection settings and API secrets.

## systemd Service

Create `/etc/systemd/system/air360-backend.service`:

```ini
[Unit]
Description=Air360 API Backend
After=network.target

[Service]
Type=simple
User=air360
Group=air360
WorkingDirectory=/opt/air360/backend
EnvironmentFile=/etc/air360-backend.env
ExecStart=/usr/local/bin/node dist/server.js
Restart=always
RestartSec=3
KillSignal=SIGINT
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

Reference file in this repository:

- `docs/backend/air360-backend.service`

Enable and start the service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable air360-backend
sudo systemctl start air360-backend
sudo systemctl status air360-backend
```

Live logs:

```bash
journalctl -u air360-backend -f
```

## nginx Reverse Proxy

Create `/etc/nginx/sites-available/air360-backend`:

```nginx
server {
    listen 80;
    server_name api.air360.ru;

    client_max_body_size 5m;

    location / {
        proxy_pass http://127.0.0.1:3000;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

Reference file in this repository:

- `docs/backend/air360-backend.nginx.conf`

Enable the site:

```bash
sudo ln -s /etc/nginx/sites-available/air360-backend /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

## TLS With Let's Encrypt

Once DNS for `api.air360.ru` points to the server, install Certbot and issue the certificate:

```bash
sudo snap install --classic certbot
sudo ln -s /snap/bin/certbot /usr/bin/certbot
sudo certbot --nginx -d api.air360.ru
sudo certbot renew --dry-run
```

## First Runtime Checks

Once the service is running:

- confirm `nginx` is healthy
- confirm `air360-backend.service` is healthy
- request `GET /health`
- verify that the Fastify app is not exposed directly on a public port

Example checks:

```bash
systemctl status nginx
systemctl status air360-backend
curl -i http://127.0.0.1:3000/health
curl -i https://api.air360.ru/health
ss -ltnp
```

## Database Notes For Later

The current backend scaffold does not require a database yet.

When implementation reaches persistence:

- install `PostgreSQL` from the official PostgreSQL Ubuntu repository if a newer server version is needed
- install `TimescaleDB` as a PostgreSQL extension using the self-hosted Timescale documentation
- keep both services private to the host or private network
- start with one PostgreSQL instance plus the TimescaleDB extension unless real scale or isolation requirements justify splitting them

That keeps the first deployment operationally simple while preserving the logical split between:

- user and device control-plane data
- time-series telemetry storage

## Operational Notes

- do not expose the raw Node.js port publicly
- do not store secrets in the repository
- keep the backend running as the dedicated `air360` user
- renew system packages regularly
- back up the environment file and future database state
- if upload sizes grow later, revisit `client_max_body_size` and Fastify body limits

## Update Flow

For a manual update on the same server:

```bash
cd /opt/air360/backend
sudo -u air360 git pull
sudo -u air360 npm ci
sudo -u air360 npm run build
sudo systemctl restart air360-backend
sudo systemctl status air360-backend
```

To standardize the same flow, use the release deployment script from this repository:

- `docs/backend/air360-backend-release-deploy.sh`

Example usage on the server:

```bash
sudo bash air360-backend-release-deploy.sh main
sudo bash air360-backend-release-deploy.sh v0.1.0
```

The script performs these steps:

1. enters the backend directory
2. fetches tags and remote branches
3. checks out the requested branch or tag
4. hard-resets to `origin/<branch>` when the target is a remote branch
5. runs `npm ci`
6. runs `npm run build`
7. restarts the `systemd` service
8. prints service status
9. checks the local health endpoint

Review the variables at the top of the script before first use:

- `APP_USER`
- `APP_DIR`
- `SERVICE_NAME`

Important:

- the script assumes the server checkout is a deployment working copy, not a place for manual edits
- when the target is a branch name, it force-aligns that branch to `origin/<branch>`
- if local uncommitted changes exist in the deployment directory, they may be discarded during branch-based updates

## External References

- Node.js release schedule and active branches: <https://nodejs.org/en/about/previous-releases>
- Node.js latest `v24.x` downloads: <https://nodejs.org/dist/latest-v24.x/>
- Ubuntu firewall documentation: <https://ubuntu.com/server/docs/how-to/security/firewalls>
- nginx proxy module documentation: <https://nginx.org/en/docs/http/ngx_http_proxy_module.html>
- Certbot Ubuntu + nginx instructions: <https://certbot.eff.org/instructions?ws=nginx&os=ubuntufocal>
- PostgreSQL Ubuntu packages: <https://www.postgresql.org/download/linux/ubuntu/>
- Timescale self-hosted install docs: <https://docs.timescale.com/self-hosted/latest/install/installation-linux/>
