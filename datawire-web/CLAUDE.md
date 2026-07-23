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

## Layout (cargo workspace)

| Crate | Role |
|-------|------|
| `shared/` | `Series`/`SeriesMeta`/`Observation` (serde) — one model, server serializes, client deserializes, contract can't drift |
| `server/` | axum: `GET /api/series/:id` (proxy+normalise FRED), serves `client/dist`. Key from `FRED_API_KEY` or the shared credentials file |
| `client/` | eframe + egui_plot → WASM: fetches `/api/series/…` via `ehttp`, plots interactively. Native `cargo run` also works for quick iteration |

Stack: eframe 0.35 (App uses `fn ui(&mut Ui,…)`), egui_plot 0.36, ehttp 0.5,
axum 0.7, reqwest 0.12, tokio. Bundler: `trunk`, target `wasm32-unknown-unknown`.

## Run

```sh
cd datawire-web
(cd client && trunk build)        # build the WASM bundle -> client/dist
cargo run -p datawire-server      # serves http://127.0.0.1:8080 (API + page)
# open http://127.0.0.1:8080
```
Needs a FRED key (env or the shared credentials file) and the wasm target + trunk
(`rustup target add wasm32-unknown-unknown`, `cargo install trunk`).

## Status

Vertical slice done & server-side verified live: server proxies FRED (UNRATE/CPI,
real obs), client compiles to WASM and fetches one hardcoded series (`UNRATE`) into
an interactive egui_plot. **Not verified headless:** the in-browser render / zoom /
pan (open it and confirm).

## Next

- Client: pick the series (route/dropdown), window controls (1Y/5Y/MAX) reusing the
  same window math, proper date x-axis formatter.
- Then the full board: signal list (from `watchlist.txt` / a `/api/watchlist`) ↔
  chart, and `/api/search` wired to an add flow.
- Dev ergonomics: `trunk serve` with a `[proxy]` to the axum backend for hot reload.

## Conventions

Commits: `datawire-web: <what>`, short, single line, no co-author/tags. Deps via
cargo. `target/` and `dist/` git-ignored; `Cargo.lock` committed.
