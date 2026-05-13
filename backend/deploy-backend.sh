#!/usr/bin/env sh
set -eu

APP_USER="${AIR360_BACKEND_USER:-air360}"
APP_GROUP="${AIR360_BACKEND_GROUP:-air360}"
APP_DIR="${AIR360_BACKEND_DIR:-/opt/air360/backend}"
SERVICE_NAME="${AIR360_BACKEND_SERVICE:-air360-backend}"

if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    SUDO="sudo"
fi

$SUDO chown -R "$APP_USER:$APP_GROUP" "$APP_DIR"
cd "$APP_DIR"

sudo -u "$APP_USER" npm ci
sudo -u "$APP_USER" npm run build
$SUDO systemctl restart "$SERVICE_NAME"
