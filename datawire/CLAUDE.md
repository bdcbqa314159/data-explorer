# datawire — project charter

Sibling to `../feedwire`. Same DNA (many fragmented sources → one curated place,
tailored per user), pointed at **statistical data portals** instead of news.

## Aim

An analyst's **signal board**: keep a per-client watchlist of specific data series
across several portals, and see them in one stripped terminal view — latest value,
change, sparkline, and a live detail pane — with a deep-link back to the source.
Kills the "grabbing data, jumping site to site" pain.

## Guiding principle (this leads every decision)

> **The analyst is the expert. datawire is a flexible instrument, not an oracle.**
> UX and flexibility lead the technology. Quantitative methods come **later, as an
> opt-in lens the analyst reaches for** — never an automatic verdict the tool imposes.

Concretely: the tool arranges nothing, ranks nothing, concludes nothing on its own.
It holds the analyst's **groupings** and the analyst's **notes**. Anything "smart"
(compare, YoY, spread, z-score) is something the user invokes on a signal they chose.

## Sources (primary job: SEE THE DATA in one place)

| Source | Access | Notes |
|---|---|---|
| Federal Reserve | FRED API (JSON, free key) + DDP | cleanest — build first |
| Eurostat | REST API (JSON-stat / SDMX) | keyless |
| Banque de France | Webstat API (SDMX/XML) | SDMX is fiddly → later phase |
| Philadelphia Fed (credit card / mortgage) | periodic file releases (Excel/CSV) | no real API → parse files |
| *(verify exact endpoints per source at build time of its phase)* | | |

## Architecture — one model, many adapters (the core lesson)

Every source speaks a different dialect but reduces to a time series. The common
`Series` type is an **anti-corruption layer**: the UI, cache, and future web layer
never know which portal a number came from. New source = one new adapter, nothing
else changes.

```
Series {
  id            "FRED:UNRATE"
  name          "US Unemployment Rate"
  source        "FRED"
  unit          "%"        frequency  Monthly
  observations  [(date, value), …]
  asOf          latest date
  sourceUrl     deep-link back to the portal
}

Adapter (one per source):
  Series           fetch(SignalRef)          // watchlist item -> series
  vector<SignalRef> search(string query)     // catalog search (MVP needs this)
```

**Tailoring primitive:** a per-client **watchlist** (like feedwire's `feeds.txt`) —
signals grouped into named boards. In-app add writes back to it.

## Interaction model (agreed with the UX lead)

Two-pane board, feedwire-consistent. Left = grouped signal list; right = **live
detail** (chart + recent obs + notes) for the selection. Progressive disclosure:
glance (list) → detail (right pane, always on) → source (deep-link).

```
┌ datawire ─────────────────────────────────────── ● US macro  Credit  Europe ┐
│ GROWTH & LABOR                 │ US Unemployment Rate                        │
│› US Unemployment   4.1% ▲0.1   │ FRED · UNRATE · Monthly           [1Y]5Y MAX│
│  Nonfarm Payrolls +147k ▼32k   │  4.0┤─╮  ╭──╯╰─╮   ╭──╮   ╭─────╮╭─         │
│ PRICES                         │  3.5┤ ╰──╯     ╰───╯  ╰───╯     ╰╯          │
│  US CPI YoY        3.0% ▬0.0   │     └┬──────┬──────┬──────┬──────           │
│  EA HICP YoY       2.4% ▼0.2   │ RECENT      NOTES ─────────────────────     │
│ CREDIT                         │ Jun26 4.1   watching sahm-rule; x-check     │
│  CC Delinquency   3.05% ▲0.12  │ May26 4.0   payrolls + claims — me 07-20    │
└────────────────────────────────┴─────────────────────────────────────────────┘
 j/k select · →/↵ focus detail · w window · o open · a add · m move · tab board · q
```

**Catalog search (Screen C, `a` — IN THE MVP):** search a source's catalog inside
the tool, add with a keystroke — no need to know series IDs. This is the flexibility
that leads the value.
```
┌ Add signal ──────  source:[FRED ▾]  ⌕ mortgage delinquency______  ┐
│› Delinquency Rate on Single-Family Mortgages   DRSFRMACBS  Q  FRED │
│  Delinquency Rate on Credit Card Loans         DRCCLACBS   Q  FRED │
│ ↵ preview · space add to [US macro] · tab source · esc cancel     │
└───────────────────────────────────────────────────────────────────┘
```

Keymap (vim-flavored, matches feedwire): `j/k`/arrows select · `↵`/`→` focus detail
· `o` open source · `a`/`/` add/find · `w` window (1Y/5Y/MAX) · `m` move · `g` group
· `tab` board · `q`/`←` quit/back.

## Roadmap

| Phase | Adds | New ground |
|---|---|---|
| 0 — MVP | `Series` + FRED adapter + two-pane board + **catalog search & add** + watchlist persistence | adapter pattern, JSON, sparkline/chart rendering |
| 1 — Flexibility | reorder `m`, group `g`, multiple boards `tab`, notes, window `w` | arrangement UX |
| 2 — Eurostat adapter | second source | proves the abstraction holds |
| 3 — More sources | Banque de France (SDMX) + Philly Fed (file releases) | awkward/heterogeneous formats |
| 4 — Web | htmx web board reusing the core | the A→B web move |
| later — Quant lens (opt-in) | `c` compare, derived series (YoY / spread / z-score) | analyst-invoked only |

## Web direction (later, after the terminal prototype)

Same A→B thinking as feedwire's Rust/htmx plan: the fetch→normalize (`Series`)
core is the reusable engine; the htmx web board is a thin server-rendered shell
over it. **No FFI bridge** — if the web layer is Rust, it's a standalone sibling
sharing the watchlist file contract, not a linked ABI. Decide C++-web-vs-Rust-web
when we reach Phase 4.

## Likely deps (FetchContent from source, like feedwire)

libcurl (fetch) · FTXUI (TUI) · a JSON lib (FRED/Eurostat JSON) · pugixml/libxml2
(SDMX XML, later). Sparklines/charts rendered with Unicode blocks (custom, small —
no chart lib unless it clearly pays off).

## Conventions (inherited from feedwire)

- Deps built from source via CMake FetchContent; `cmake --preset default`.
- Tests: plain asserts + CTest, no framework. Pure logic (adapters' parse step,
  `Series` math) unit-tested without network.
- Commit messages: short, single line, `datawire: <what>`. No co-author lines, no tags.
- Separation: pure parse/normalize logic has no I/O; I/O and UI in their own modules.

## Status — Phase 0 in progress

Built & live-verified: `Series` model, FRED adapter (`series`, `series/observations`,
`series/search`) with parser tests, secure API-key storage (0600 file + env override),
and the **two-pane board** (grouped list ↔ live detail with FTXUI chart, `w` window,
`o` open source). Data path confirmed against live FRED.

CLI: `datawire` (board) · `datawire get <ID>` · `datawire search <text>` · `datawire key set`.
Watchlist: `watchlist.txt` (`# Group` headers + FRED ids).

**Not verified headless** (drive the board in a terminal): list scroll-follow,
chart rendering, `w` window toggle, `o` open.
**Still to do in Phase 0:** in-app catalog search overlay (`a`) — currently search is
CLI-only; wiring it into the board (add-to-watchlist) is the next slice.
