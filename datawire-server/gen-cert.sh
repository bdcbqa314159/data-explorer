#!/usr/bin/env bash
# Generate a self-signed dev certificate for the local HTTPS server.
# Output (gitignored): certs/server.crt + certs/server.key. Dev only — not for
# production. The terminal trusts this cert via DATAWIRE_SERVER_CA.
set -eu
DIR="$(cd "$(dirname "$0")" && pwd)/certs"
mkdir -p "$DIR"

if [ -f "$DIR/server.crt" ] && [ -f "$DIR/server.key" ] && [ "${1:-}" != "-f" ]; then
  echo "cert already present ($DIR/server.crt); pass -f to regenerate"
  exit 0
fi

openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout "$DIR/server.key" -out "$DIR/server.crt" -days 3650 \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" >/dev/null 2>&1

chmod 600 "$DIR/server.key"
echo "wrote $DIR/server.crt and $DIR/server.key (CN=localhost, 10y)"
