#!/usr/bin/env bash

set -euo pipefail

APP_NAME="air360-backend"
APP_USER="air360"
APP_DIR="/opt/air360/backend"
SERVICE_NAME="air360-backend"
TARGET_REF="${1:-main}"

echo "==> 1. Switching to application directory"
cd "${APP_DIR}"

echo "==> 2. Fetching latest repository state"
sudo -u "${APP_USER}" git fetch --tags --prune origin

echo "==> 3. Checking out target ref: ${TARGET_REF}"
sudo -u "${APP_USER}" git checkout "${TARGET_REF}"

echo "==> 4. Resetting local branch to origin when target is a branch name"
if sudo -u "${APP_USER}" git show-ref --verify --quiet "refs/remotes/origin/${TARGET_REF}"; then
  sudo -u "${APP_USER}" git reset --hard "origin/${TARGET_REF}"
fi

echo "==> 5. Installing exact dependencies from lockfile"
sudo -u "${APP_USER}" npm ci

echo "==> 6. Building release artifacts"
sudo -u "${APP_USER}" npm run build

echo "==> 7. Restarting systemd service"
systemctl restart "${SERVICE_NAME}"

echo "==> 8. Showing service status"
systemctl --no-pager --full status "${SERVICE_NAME}"

echo "==> 9. Checking local health endpoint"
curl -fsS http://127.0.0.1:3000/health

echo
echo "${APP_NAME} deployment finished successfully."
