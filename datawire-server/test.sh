#!/usr/bin/env bash
# End-to-end test for datawire-server: DB store self-test + live HTTP endpoint
# assertions. Needs a running MySQL and the built binaries in ./build.
#   ctest integration: exits 77 (SKIP) when MySQL is unreachable.
set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$DIR/build/datawire-server"
SELFTEST="$DIR/build/datawire-server-selftest"
HOST="${DATAWIRE_DB_HOST:-127.0.0.1}"
USER="${DATAWIRE_DB_USER:-root}"
PORT="8099"                       # test port (avoids a dev server on :8080)
CERT="$DIR/certs/server.crt"
BASE="https://127.0.0.1:$PORT"
PW_ARG=""; [ -n "${DATAWIRE_DB_PASS:-}" ] && PW_ARG="--password=$DATAWIRE_DB_PASS"

# 0. MySQL reachable? Otherwise skip (not fail) — ctest SKIP_RETURN_CODE=77.
if ! mysqladmin --host="$HOST" --user="$USER" $PW_ARG ping >/dev/null 2>&1; then
  echo "SKIP: MySQL not reachable (brew services start mysql)"; exit 77
fi
[ -x "$BIN" ] && [ -x "$SELFTEST" ] || { echo "FAIL: build first (cmake --build build)"; exit 1; }
"$DIR/gen-cert.sh" >/dev/null 2>&1 || { echo "FAIL: gen-cert.sh"; exit 1; }  # ensure TLS cert

SV=""
cleanup() {
  if [ -n "$SV" ]; then kill "$SV" 2>/dev/null; wait "$SV" 2>/dev/null; fi
  mysql --host="$HOST" --user="$USER" $PW_ARG -e \
    "DELETE FROM datawire.observation WHERE series_id='$ID'; DELETE FROM datawire.series WHERE id='$ID';" 2>/dev/null
}
trap cleanup EXIT
fail() { echo "FAIL: $1"; exit 1; }

# 1. DB store round-trip (meta + observations, wholesale replace)
"$SELFTEST" >/dev/null || fail "selftest (DB round-trip)"

# 2. Start the server on the test port
DATAWIRE_PORT="$PORT" "$BIN" >/tmp/dws_test.log 2>&1 & SV=$!
curl -s --cacert "$CERT" --retry-connrefused --retry 20 --retry-delay 1 "$BASE/health" >/dev/null || fail "server did not start (see /tmp/dws_test.log)"

# 3. /health
[ "$(curl -s --cacert "$CERT" "$BASE/health")" = '{"status":"ok"}' ] || fail "/health body"

# 4. POST /series
ID="ITEST_$$"
curl -s --cacert "$CERT" -X POST "$BASE/series" -d \
  "{\"meta\":{\"id\":\"$ID\",\"title\":\"IT\",\"frequency\":\"Monthly\",\"source\":\"TEST\"},\"observations\":[{\"date\":\"2026-01-01\",\"value\":1.5},{\"date\":\"2026-02-01\",\"value\":2.5}]}" \
  | grep -q '"stored"' || fail "POST /series"

# 5. GET /series/:id — title + exactly 2 observations survive the round-trip
GOT="$(curl -s --cacert "$CERT" "$BASE/series/$ID")"
echo "$GOT" | grep -q '"title":"IT"' || fail "GET /series/:id title"
N=$(echo "$GOT" | grep -o '"date"' | wc -l | tr -d ' ')
[ "$N" = "2" ] || fail "GET /series/:id observation count = $N (want 2)"

# 6. GET /series — list contains it
curl -s --cacert "$CERT" "$BASE/series" | grep -q "\"$ID\"" || fail "GET /series list"

# 7. missing id -> 404
CODE=$(curl -s --cacert "$CERT" -o /dev/null -w "%{http_code}" "$BASE/series/NOPE_$$")
[ "$CODE" = "404" ] || fail "missing id: want 404, got $CODE"

echo "PASS: datawire-server — selftest + 5 endpoint checks ($ID)"
