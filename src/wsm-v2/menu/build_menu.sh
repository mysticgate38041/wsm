#!/usr/bin/env bash
# Compatibility entry point: the pinned Windows build also includes native verification.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/../scripts/build.ps1")" "$@"
