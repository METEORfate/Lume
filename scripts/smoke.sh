#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-$ROOT_DIR/build/lume_server}"
PORT="${LUME_SMOKE_PORT:-18080}"
TMP_DIR="$(mktemp -d)"
SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -rf "$TMP_DIR"
}

trap cleanup EXIT

mkdir -p "$TMP_DIR/public/assets"
printf '<!doctype html><title>ok</title><link rel="stylesheet" href="/style.css">' > "$TMP_DIR/public/index.html"
printf 'body { color: #123; }\n' > "$TMP_DIR/public/style.css"
printf 'console.log("ok");\n' > "$TMP_DIR/public/app.js"
printf 'fake image bytes\n' > "$TMP_DIR/public/assets/image.png"
dd if=/dev/zero of="$TMP_DIR/public/assets/large.bin" bs=1024 count=512 status=none
cat > "$TMP_DIR/server.conf" <<EOF_CONF
PORT=$PORT
ROOT_DIR=$TMP_DIR/public
MAX_REQUEST_BYTES=16384
MAX_CONNECTIONS=128
REQUEST_TIMEOUT=3
EOF_CONF

"$BIN" "$TMP_DIR/server.conf" > "$TMP_DIR/server.log" 2>&1 &
SERVER_PID="$!"

for _ in {1..50}; do
  if curl --noproxy '*' -fsS "http://127.0.0.1:$PORT/" > "$TMP_DIR/index.out" 2>/dev/null; then
    break
  fi
  sleep 0.1
done

curl --noproxy '*' -fsS "http://127.0.0.1:$PORT/style.css" > "$TMP_DIR/style.out"
curl --noproxy '*' -fsS "http://127.0.0.1:$PORT/app.js" > "$TMP_DIR/app.out"
curl --noproxy '*' -fsS "http://127.0.0.1:$PORT/assets/image.png" > "$TMP_DIR/image.out"
curl --noproxy '*' -fsS "http://127.0.0.1:$PORT/assets/large.bin" > "$TMP_DIR/large.out"

missing_status="$(curl --noproxy '*' -sS -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/missing.html")"
post_status="$(curl --noproxy '*' -sS -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$PORT/")"
traversal_status="$(curl --noproxy '*' -sS -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/%2e%2e/etc/passwd")"
set +o pipefail
long_header="$(head -c 20000 /dev/zero | tr '\0' 'a')"
set -o pipefail
long_header_status="$(curl --noproxy '*' -sS -o /dev/null -w '%{http_code}' -H "X-Lume-Long: $long_header" "http://127.0.0.1:$PORT/")"

[[ "$missing_status" == "404" ]]
[[ "$post_status" == "501" ]]
[[ "$traversal_status" == "400" ]]
[[ "$long_header_status" == "400" ]]
[[ "$(wc -c < "$TMP_DIR/large.out")" == "524288" ]]

echo "smoke test passed on port $PORT"
