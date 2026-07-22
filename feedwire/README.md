# feedwire

A personal terminal news aggregator. You give it a list of feed URLs; it fetches
them all at once, merges them into one time-sorted stream, and prints a single
curated view — so you read one place instead of browsing five sites.

**Phase 0** was the spine: fetch → parse → dedup → sort → print.
**Phase 1** (current) adds a disk TTL cache, search/source filters, and
read/unread tracking — all on top of that spine, no rewrite.

```
*  12m  CNBC          Fed holds rates steady, signals one cut in 2026
*  34m  CoinDesk      Bitcoin reclaims $90k as ETF inflows resume
   51m  Hacker News   Show HN: I built a terminal news reader in C++
```
(A leading `*` marks an unread story.)

---

## Design in one paragraph

`main.cpp` reads `feeds.txt`, then fans out **one `std::async` task per feed**.
Each task does an HTTP GET (`http_client` — a thin RAII wrapper over libcurl) and
parses the response (`feed` — RSS 2.0 + Atom via pugixml, plus HTML-strip and a
toy sentiment tag). Results are merged, deduped by URL, sorted newest-first, and
printed. A dead feed logs a warning and contributes nothing — it never sinks the
batch. That's the whole program.

| File | Responsibility |
|------|----------------|
| `src/http_client.{hpp,cpp}` | libcurl RAII wrappers; `httpGet(url)` (thread-safe per call) |
| `src/feed.{hpp,cpp}` | `parseFeed`, `stripHtml`, `inferSentiment` — pure, no I/O |
| `src/cache.{hpp,cpp}` | `FeedCache` — disk TTL cache of raw feed bodies, with stale fallback |
| `src/read_store.{hpp,cpp}` | `ReadStore` — persistent set of read item urls |
| `src/main.cpp` | args → cache-aware concurrent fetch → merge/dedup/sort → filter → print |
| `tests/feed_test.cpp` | parsing-layer tests (run via CTest) |
| `tests/store_test.cpp` | cache + read-store tests, using a temp dir |

The I/O layer (`http_client`) and the pure logic (`feed`) are kept separate so the
parser is trivially testable without a network. `cache` and `read_store` are
best-effort: any filesystem error degrades to "no cache" instead of throwing.

### Fetch path (Phase 1)

Per feed: **fresh cache?** use it → else **fetch** and cache it → else (network
down) **fall back to any cached copy, even stale**. So a warm run is instant and
offline, and a network blip still shows the last-known stories.

---

## Dependencies (built from source)

Two libraries, both compiled from source per-platform by **vcpkg** in *manifest
mode* (declared in `vcpkg.json`):

- **libcurl** — HTTP + TLS. vcpkg selects a working TLS backend for each OS
  (Schannel on Windows, a suitable one on macOS/Linux), so HTTPS feeds work out
  of the box.
- **pugixml** — small, fast XML parser for RSS/Atom.

You do **not** run any `apt install` / `brew install` for these — vcpkg fetches
and builds them. The only host prerequisites are a C++20 compiler, CMake ≥ 3.21,
and git.

---

## Build & run

### 1. Get vcpkg (once, from source)

```sh
git clone https://github.com/microsoft/vcpkg
./vcpkg/bootstrap-vcpkg.sh        # Windows: .\vcpkg\bootstrap-vcpkg.bat
```

Point an environment variable at it so the CMake preset can find the toolchain:

```sh
export VCPKG_ROOT=/absolute/path/to/vcpkg      # bash/zsh
# Windows PowerShell:  $env:VCPKG_ROOT = "C:\path\to\vcpkg"
```

(Persist it in your shell profile so you don't repeat this.)

### 2. Configure, build, test

From the `feedwire/` directory:

```sh
cmake --preset default        # first run builds libcurl + pugixml from source (slow, one-time)
cmake --build build
ctest --preset default        # runs the parser tests
```

### 3. Run

```sh
./build/feedwire                 # all stories, newest first
./build/feedwire --unread        # only what's new since you last caught up
./build/feedwire --search bitcoin
./build/feedwire --source CNBC
./build/feedwire --mark-read     # mark the shown stories read, then exit
./build/feedwire --help
```

On Windows the binary lands at `build\Release\feedwire.exe` (multi-config
generator).

### Flags

| Flag | Effect |
|------|--------|
| `--config PATH` / positional | feed list (default `feeds.txt`) |
| `--search TERM` | keep stories whose title/summary contains TERM (case-insensitive) |
| `--source NAME` | keep stories from one feed (case-insensitive) |
| `--ttl MINUTES` | cache freshness window (default 10) |
| `--no-cache` | always refetch, ignore fresh cache |
| `--unread` | only stories not yet marked read |
| `--mark-read` | mark the shown stories read, then exit |

A typical loop: `feedwire --unread` to see what's new, then `feedwire --mark-read`
to catch up.

### State on disk (`.cache/`)

- `feed_<hash>.txt` — one cached feed body each (first line = fetch time).
- `read.txt` — urls you've marked read, one per line.

Delete `.cache/` any time to reset; it's rebuilt on the next run and is
git-ignored.

---

## Prerequisites by platform

| Platform | Compiler | Also need |
|----------|----------|-----------|
| Linux    | GCC ≥ 11 or Clang ≥ 14 | `cmake`, `git`, `pkg-config`, `curl`, `zip`, `unzip`, `tar` (for vcpkg) |
| macOS    | Apple Clang (Xcode CLT) | `cmake`, `git` (`xcode-select --install`) |
| Windows  | MSVC (VS 2022 Build Tools) | `cmake`, `git` |

Ninja is optional but recommended (`cmake --preset default -G Ninja` or add a
`"generator"` to the preset). Without it, CMake uses the platform default (Make
on Unix, Visual Studio on Windows).

---

## Editing the feed list

`feeds.txt`, one entry per line:

```
Name|https://example.com/rss.xml
```

- Lines starting with `#` are comments.
- A line with no `|` is treated as URL-only.
- Both RSS 2.0 and Atom feeds work.

---

## Troubleshooting

- **`Could not find toolchain file`** — `VCPKG_ROOT` is unset or wrong. Echo it and
  re-run `cmake --preset default`.
- **First configure is very slow** — expected: vcpkg is compiling libcurl and its
  TLS backend from source. Subsequent configures reuse the cache.
- **A feed prints `[warn] ...: GET failed`** — that feed timed out or blocked the
  request; the rest still render. Try the URL in a browser to confirm it's live.
- **Empty output but no warnings** — the feeds returned non-RSS/Atom XML (e.g. an
  HTML error page). Check the URL is a real feed endpoint.

---

## Roadmap

| Phase | Adds | New C++ ground | Status |
|-------|------|----------------|--------|
| 0 — Spine | fetch → parse → dedup → sort → print | RAII over C API, `std::async`, move semantics | ✅ done |
| 1 — State | disk TTL cache, `--search`/`--source`, read/unread | `std::filesystem`, caching, `std::optional`, `std::atomic` | ✅ done |
| 2 — TUI | two-pane list ↔ read-through, keyboard nav | FTXUI, event loop, render/state separation | next |
| 3 — Read-through | fetch + extract article body | a real HTML parser (gumbo/lexbor) | later |

Each phase is an optional upgrade on the one before.
