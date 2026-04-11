#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [[ ! -f ".venv/bin/python" ]]; then
  echo "Virtual environment missing in brain_server/.venv"
  echo "Create it with: python3 -m venv .venv"
  exit 1
fi

while true; do
  echo "[$(date -Iseconds)] Starting Brain Server..."
  source .venv/bin/activate
  export BRAIN_CONFIG="$SCRIPT_DIR/config.json"
  python server.py || true
  echo "[$(date -Iseconds)] Brain Server exited. Restarting in 5s..."
  sleep 5
done
