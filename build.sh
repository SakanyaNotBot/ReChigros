#!/usr/bin/env sh
# Thin wrapper around scripts/build.ps1 for Linux and macOS hosts.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

SHELL_BIN=
for candidate in pwsh powershell; do
  if command -v "$candidate" >/dev/null 2>&1; then
    SHELL_BIN=$candidate
    break
  fi
done

if [ -z "$SHELL_BIN" ]; then
  echo 'PowerShell 5.1 or newer is required to run scripts/build.ps1.' >&2
  exit 1
fi

exec "$SHELL_BIN" -NoProfile -ExecutionPolicy Bypass -File "$ROOT/scripts/build.ps1" "$@"
