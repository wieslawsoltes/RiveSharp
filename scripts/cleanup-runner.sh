#!/usr/bin/env bash

set -euo pipefail

if [[ "${RUNNER_OS:-$(uname -s)}" != "Linux" ]]; then
  echo "cleanup-runner.sh: skipping (host is not Linux)" >&2
  exit 0
fi

echo "Cleaning runner disk space..."
df -h

PURGE_PATHS=(
  /usr/share/dotnet
  /usr/local/lib/android
  /opt/android
  /opt/ghc
  /opt/hostedtoolcache
)

for path in "${PURGE_PATHS[@]}"; do
  if [[ -d "${path}" ]]; then
    sudo rm -rf "${path}"
    echo "Removed ${path}"
  fi
done

sudo apt-get clean

if command -v docker >/dev/null 2>&1; then
  sudo docker image prune --all --force >/dev/null 2>&1 || true
fi

df -h
echo "Disk cleanup complete."
