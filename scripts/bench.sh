#!/usr/bin/env bash
set -euo pipefail

URL="http://127.0.0.1:8080/"

echo "Start benchmark: $URL"

wrk -t8 -c1000 -d30s --latency $URL