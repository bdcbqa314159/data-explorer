# datawire-web — charter

The **browser frontend** for datawire. Sibling to `../datawire` (C++ terminal) and
`../feedwire`. Chosen because the analyst wants to view/zoom/pan series in a
**web browser** with **no JavaScript** — so: **Rust + egui_plot compiled to WASM**
(ImPlot-grade interactive charts, written in pure Rust) served by an **axum** backend.

## Why this shape

- **egui_plot → WASM**: real drag-pan / scroll-zoom / tooltips in the browser, zero
  hand-written JS (trunk generates the loader; wasm `main` starts eframe on a canvas).
- **axum server**: holds the FRED API key **server-side** (client never sees it),
  proxies + normalises FRED (also solves CORS), and serves the WASM bundle.
- **No FFI to the C++ core** (charter rule). Shared contract instead: the server
  reads the **same** `~/.config/datawire/credentials` the C++ tool writes, and the
  FRED adapter is a small Rust port of the C++ one (endpoints/quirks already de-risked).

## Layout (cargo workspace) — one core, three faces

| Crate | Role |
|-------|------|
| `shared/` | model + wire types (serde): `Series`/`Observation`/`WatchItem`/`SearchResult`/`AddRequest` |
| `server/` | **lib + bin.** `api_router()` (the `/api` routes: watchlist GET/POST, search, series) reusable by others; bin also serves `client/dist`. FRED proxy + `watchlist.txt` I/O. Key via `FRED_API_KEY` or the shared credentials file. rustls TLS (no OpenSSL) |
| `client/` | **lib + bin.** `App::new(cc, base_url)` — the whole egui/egui_plot board (list+Δ, window buttons, hover, search/add). `base=""` for browser (wasm bin, served by server), `base="http://127.0.0.1:PORT"` for native |
| `desktop/` | **native bin.** Embeds the server on an ephemeral localhost port + runs the client UI → one self-contained offline binary, every feature, no browser/CORS |

Stack: eframe 0.35 (`fn ui(&mut Ui,…)`, panels are `Panel::left`/`.show(ui,…)`),
egui_plot 0.36, ehttp 0.5, axum 0.7, reqwest 0.12 (rustls-tls), tokio.
Web bundler: `trunk`, target `wasm32-unknown-unknown`.

## Run

**Browser** (needs wasm target + trunk):
```sh
cd datawire-web && (cd client && trunk build) && cargo run -p datawire-server
# open http://127.0.0.1:8080
```
**Native desktop** (no wasm/trunk needed — offline-friendly):
```sh
cd datawire-web && cargo run -p datawire-desktop
```
Both need a FRED key (env `FRED_API_KEY` or the shared credentials file). Desktop
seeds a starter `watchlist.txt` next to the credentials file on first run.

## Offline / network-gated build (the C++-style "from source" story)

Cargo downloads crate SOURCE from crates.io once, then builds locally + caches.
For a locked-down machine: `cargo vendor` on a connected box → commit `vendor/` +
`.cargo/config.toml` → clone from GitHub → `cargo build --offline`. The **native
desktop** path needs *only* vendored crates (no wasm target, no trunk, no
wasm-bindgen) and rustls (no system TLS) → fully offline with just GitHub + rustc +
MSVC. The browser path additionally needs the wasm toolchain (harder offline).

## Status

Full board on **both** frontends, server-side verified live (FRED proxy: UNRATE 941
obs, CPI 953 obs; search + add tested). **Not verified headless:** the actual render
(browser and native window) — open and confirm.

## Next

- A `/api/board` summary endpoint if watchlists get large (rows currently eager-load).
- Groups as collapsible sections; per-series y-bounds for edge cases.
- Then a second source (Eurostat) via the same `Series` shape.

## Conventions

Commits: `datawire-web: <what>`, short, single line, no co-author/tags. Deps via
cargo. `target/` and `dist/` git-ignored; `Cargo.lock` committed.
