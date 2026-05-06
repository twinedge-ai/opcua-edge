#!/usr/bin/env sh
set -eu

endpoint="${1:-opc.tcp://127.0.0.1:4840}"
if [ "$#" -gt 0 ]; then
  shift
fi

if ! command -v npx >/dev/null 2>&1; then
  echo "npx is required. Install Node.js/npm first, for example: sudo apt install nodejs npm" >&2
  exit 127
fi

exec npx --yes opcua-commander -e "$endpoint" "$@"
