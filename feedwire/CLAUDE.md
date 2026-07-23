# feedwire — project charter

## Aim

A personal terminal news aggregator: gather stories from many feeds and read them
in **one curated place** instead of browsing several sites. Secondary aim (equally
important): a **learning vehicle for modern C++** — each feature is chosen to teach
a distinct piece of the language, built lazily (stdlib/native first, fewest deps).

Keep it small. Prefer deleting over adding. Mark deliberate shortcuts with a
`ponytail:` comment naming the ceiling.

## Status — C++ prototype (Phases 0–3 done)

- **0 Spine** — fetch → parse (RSS/Atom) → dedup → sort → print. `std::async` fan-out, RAII over libcurl.
- **1 State** — disk TTL cache (stale fallback offline), `--search`/`--source`, read/unread (`--unread`/`--mark-read`). `std::filesystem`, `std::atomic`.
- **2 TUI** — `--tui` two-pane reader (FTXUI). Selected headline marquee-scrolls; browsing marks read.
- **3 Read-through** — select a story → background fetch → libxml2 article extraction into the right pane; line/page scroll (`space`/`b`); fetch is abortable on quit.

Plain (non-`--tui`) output stays the default so pipes/tests keep working.

## Architecture

| File | Responsibility |
|------|----------------|
| `src/http_client.*` | libcurl RAII; `httpGet(url, timeout, cancel*)` — thread-safe, abortable |
| `src/feed.*` | `parseFeed`, `stripHtml`, `inferSentiment` — pure, no I/O |
| `src/cache.*` | `FeedCache` — disk TTL cache of bodies, best-effort (degrades, never throws) |
| `src/read_store.*` | `ReadStore` — persistent set of read urls |
| `src/article.*` | `extractArticle` — libxml2 HTML parse + XPath, prose-only |
| `src/tui.*` | `runTui` — FTXUI reader, async article loading, marquee, line scroll |
| `src/main.cpp` | args → cache-aware concurrent fetch → merge/dedup/sort → filter → print or TUI |
| `tests/*` | plain-assert tests via CTest (no framework) |

**Separation rule:** pure logic (`feed`, `article`) has no I/O so it's unit-testable
without a network. I/O and UI sit in their own modules.

## Build & deps

Deps are pulled from GitHub and **built from source via CMake FetchContent** (no
package manager). Pinned in `CMakeLists.txt`: **pugixml**, **libcurl** (native TLS:
Schannel/SecureTransport/OpenSSL), **FTXUI**, **libxml2** (HTML+XPath, lean build).

```sh
cmake --preset default && cmake --build build && ctest --preset default
./build/feedwire --tui
```
Linux also needs `libssl-dev` (curl's OpenSSL backend). Windows/macOS need nothing extra.

## Open items on the C++ side

**Not verified headless (need a real terminal — check when driving `--tui`):**
- list scroll-to-selection follows `j`/`k`/arrows past the visible window
- marquee feel/speed (`kMarqueeMsPerChar = 260`)
- article line/page scroll (`space`/`b`) and the ↑/↓ more markers
- quit during an in-flight article fetch returns promptly (cancel flag)

**Known ceilings (marked `ponytail:` in code, all fine for English feeds):**
- marquee, word-wrap, and the extraction word-filter are space/byte based → space-less
  scripts (CJK) won't scroll/wrap/extract cleanly
- JS-rendered / paywalled pages have no server-side article text → fall back to `o`
  (open in browser). Inherent, not a bug.
- feed cache and article cache are separate dirs under `.cache/`, keyed by url hash

**Not built (candidate polish, only if wanted):** in-TUI `/` search, feed management,
configurable `.cache`/`feeds.txt` location (currently cwd-relative).

## Direction — web reader via Rust + htmx (AFTER the C++ prototype)

Goal: improve the **web-URL side** — instead of dumping the user onto an ad-heavy
source page, serve a clean reader-mode page, and eventually a browsable web reader.

**Coupling policy:** do **NOT** FFI-bridge C++ ↔ Rust. The Rust web app is a
**standalone sibling binary**. The two frontends share **data files, not a linked
ABI**: both read the same `feeds.txt` (and may share the `.cache/` format). C++ stays
the TUI; Rust owns the web + extraction path.

Why Rust here: its readability ecosystem (`readability` / `dom_smoothie` /
`article_scraper` — the crate the Rust reader *Newsflash* uses) does Readability.js-style
content scoring, materially better than our libxml2 heuristic. Plus `reqwest`
(fetch), `feed-rs` (RSS/Atom), `axum` (async server). htmx = server-rendered HTML
partials, no SPA/JS framework.

**Sequence — A then B (A is a strict subset of B, never redone):**
- **A — reader-mode service (do first):** small `axum` app, `GET /read?url=` → clean
  article HTML (Rust readability) + a htmx page. Stateless (`url in → HTML out`).
  feedwire's `o` opens this local clean URL instead of the source. Highest value,
  smallest surface, independently useful.
- **B — full web reader (later):** same `axum` binary, add `GET /` (headline list from
  `feeds.txt`) and htmx wiring so clicking a headline swaps A's `/read` output into
  the pane. A thin shell around A.

Keep A and B as **one Rust binary**; B just adds routes.

## Conventions

- Commit messages: short, single line, `feedwire: <what>`. No co-author lines. No tags.
- Tests: plain asserts + CTest, no framework/fixtures. One runnable check per non-trivial unit.
- `build/` and `.cache/` are git-ignored; `build/_deps/` holds the fetched dep sources.
