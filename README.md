# data_explorer

A personal quantitative / data-engineering toolset. The flagship is **datawire** — a
terminal for exploring economic time series (FRED) with an offline-first local store, a
robust analysis toolkit, and an optional server it syncs to over TLS.

## Projects

| Dir | What it is | Stack |
|-----|------------|-------|
| **datawire/** | Terminal UI for FRED economic data — board, charts, compare, robust stats. Offline-first on SQLite; syncs to the server. | C++20, FTXUI, libcurl (mbedTLS), SQLite |
| **datawire-server/** | The shared server: owns a MySQL database, serves HTTPS/JSON. | C++20, Boost.MySQL, Boost.Beast, OpenSSL |
| **datawire-web/** | Web/desktop charting frontend. | Rust, egui/eframe, axum |
| **feedwire/** | News aggregator. | C++20 |

`datawire` and its SDK (`datawire_core`) are built entirely from source via CMake
FetchContent (offline-friendly, no system deps). The server is dev-time infra and uses
system packages (Boost/OpenSSL) + a local MySQL.

## datawire — the five pillars

1. **SDK + free data provider** — `datawire_core` with a `Source` seam; FRED behind it.
2. **Local store** — embedded SQLite, the offline source of truth.
3. **TLS + sync** — talks to the server over verified HTTPS; offline-first, syncs on demand.
4. **Secure credentials** — OS-agnostic `SecretStore` (env → OS keychain → 0600 config file).
5. **Robust analysis toolkit** — Welford stats, median/MAD, returns, volatility, rolling
   correlation/beta, Theil–Sen robust regression, outlier detection; compare two series.

## Quick start

### Build the terminal
```bash
cd datawire
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
(First build fetches curl + mbedTLS + FTXUI from GitHub — give it a few minutes.)

### Set your FRED API key (free: https://fredaccount.stlouisfed.org/apikeys)
```bash
./build/datawire key set        # guided TUI: masked entry, validated live, stored in the keychain
```
**Dev tip:** to skip the keychain permission prompt on every rebuild, export the key instead —
it takes precedence over the keychain:
```bash
export FRED_API_KEY=<your key>
```

### Run the board
```bash
./build/datawire
#   j/k select · h/l crosshair · w window · t lens · c compare · m movers · / add · ? help · q quit
```

### Bring up the server + sync (optional)
```bash
brew install mysql boost openssl && brew services start mysql
mysql -u root -e "CREATE USER IF NOT EXISTS 'root'@'127.0.0.1' IDENTIFIED BY ''; \
  GRANT ALL PRIVILEGES ON *.* TO 'root'@'127.0.0.1'; FLUSH PRIVILEGES;"

cd datawire-server
cmake -S . -B build && cmake --build build -j
./gen-cert.sh                   # self-signed dev cert (gitignored)
./build/datawire-server &       # https://127.0.0.1:8080

# from the terminal side: reconcile local SQLite <-> server over TLS
DATAWIRE_SERVER_CA=$PWD/certs/server.crt ../datawire/build/datawire sync
```

## Tests
```bash
ctest --test-dir datawire/build          # terminal: pure logic, no network/DB
ctest --test-dir datawire-server/build   # server: end-to-end (skips if MySQL is down)
```

## Environment variables
| Var | Used by | Purpose |
|-----|---------|---------|
| `FRED_API_KEY` | datawire | FRED key; overrides the keychain (CI/dev escape hatch) |
| `DATAWIRE_CA_BUNDLE` | datawire | Override the vendored CA bundle for public HTTPS |
| `DATAWIRE_SERVER_URL` / `DATAWIRE_SERVER_CA` | datawire | Server URL + cert to verify it, for `sync` |
| `DATAWIRE_DB_HOST` / `DATAWIRE_DB_USER` / `DATAWIRE_DB_PASS` | datawire-server | MySQL connection |
| `DATAWIRE_PORT` | datawire-server | Listen port (default 8080) |

## License

MIT — see [LICENSE](LICENSE).
