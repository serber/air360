#!/usr/bin/env sh
set -eu

APP_USER="${AIR360_PORTAL_USER:-air360}"
APP_GROUP="${AIR360_PORTAL_GROUP:-air360}"
APP_DIR="${AIR360_PORTAL_DIR:-/opt/air360/portal}"
SERVICE_NAME="${AIR360_PORTAL_SERVICE:-air360-portal}"

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
