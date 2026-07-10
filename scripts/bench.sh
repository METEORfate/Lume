#!/usr/bin/env bash
set -euo pipefail

URL="${1:-http://127.0.0.1:8080/}"

if command -v wrk >/dev/null 2>&1; then
  wrk -t4 -c100 -d10s "$URL"
elif command -v ab >/dev/null 2>&1; then
  ab -n 1000 -c 100 "$URL"
else
  echo "Install wrk or ab to run the benchmark." >&2
  exit 1
fi
