#include "board.hpp"

#include "analysis.hpp"
#include "datawire.hpp"
#include "series.hpp"
#include "stats.hpp"
#include "window.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <mutex>
#include <numeric>
#include <thread>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace datawire {

using namespace ftxui;
using namespace analysis;  // Transform, applyTransform, sparkline, compare stats…

namespace {

enum class Status { Loading, Loaded, Failed };

struct BoardSignal {
  std::string group;
  std::string id;
  Series series;
  Status status = Status::Loading;
};

enum class Window { Y1, Y5, MAX };
int yearsOf(Window w) { return w == Window::Y1 ? 1 : w == Window::Y5 ? 5 : 1000; }
Window nextWindow(Window w) {
  return w == Window::Y1 ? Window::Y5 : w == Window::Y5 ? Window::MAX : Window::Y1;
}

std::string padRight(std::string s, size_t w) {
  if (s.size() > w) s.resize(w);
  else s.append(w - s.size(), ' ');
  return s;
}
std::string padLeft(std::string s, size_t w) {
  if (s.size() < w) s.insert(0, w - s.size(), ' ');
  return s;
}

std::string formatValue(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, std::abs(v) >= 1000 ? "%.0f" : "%.2f", v);
  return buf;
}

// Watchlist: "# GROUP" lines start a group; other non-blank lines are FRED ids.
std::vector<std::pair<std::string, std::string>> loadWatchlist(const std::string& path) {
  std::vector<std::pair<std::string, std::string>> out;
  std::ifstream in(path);
  std::string line, group;
  while (std::getline(in, line)) {
    const auto b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) continue;
    line = line.substr(b);
    if (line[0] == '#') {
      group = line.substr(1);
      const auto g = group.find_first_not_of(" \t");
      group = g == std::string::npos ? "" : group.substr(g);
      continue;
    }
    const auto e = line.find_first_of(" \t\r\n");
    out.emplace_back(group, line.substr(0, e));
  }
  return out;
}

// Append `id` under `group`, skipping duplicates and rejecting anything that
// isn't a FRED-shaped id. Mirrors the web server's add logic.
void addToWatchlist(const std::string& path, const std::string& id, const std::string& group) {
  if (id.empty()) return;
  for (unsigned char c : id)
    if (!std::isalnum(c) && c != '.' && c != '_' && c != '-') return;

  std::ifstream in(path, std::ios::binary);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();

  std::string lastGroup;
  {
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
      const auto a = line.find_first_not_of(" \t\r");
      if (a == std::string::npos) continue;
      const auto b = line.find_last_not_of(" \t\r");
      const std::string t = line.substr(a, b - a + 1);
      if (t == id) return;  // already present
      if (t[0] == '#') {
        const auto g = t.find_first_not_of(" \t", 1);
        lastGroup = (g == std::string::npos) ? "" : t.substr(g);
      }
    }
  }

  std::ofstream out(path, std::ios::app);
  if (!out) return;
  if (!content.empty() && content.back() != '\n') out << "\n";
  if (lastGroup != group) out << "# " << group << "\n";
  out << id << "\n";
}

std::pair<std::string, Color> deltaOf(const Series& s) {
  const auto& o = s.observations;
  if (o.size() < 2) return {"", Color::GrayDark};
  const double d = o.back().value - o[o.size() - 2].value;
  char buf[24];
  if (d > 0) { std::snprintf(buf, sizeof buf, "▲%.2g", d); return {buf, Color::Green}; }
  if (d < 0) { std::snprintf(buf, sizeof buf, "▼%.2g", -d); return {buf, Color::Red}; }
  return {"▬", Color::GrayLight};
}

// Recent move magnitude (|% change| of the latest point) — the movers sort key.
double moverScore(const BoardSignal& s) {
  if (s.status != Status::Loaded) return -1.0;  // unloaded sinks to the bottom
  return recentMovePct(s.series.observations);
}

// Row order: watchlist order normally, biggest-mover-first in movers mode.
std::vector<int> displayOrder(const std::vector<BoardSignal>& sigs, bool movers) {
  std::vector<int> order(sigs.size());
  std::iota(order.begin(), order.end(), 0);
  if (movers)
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return moverScore(sigs[a]) > moverScore(sigs[b]); });
  return order;
}

Element signalRow(const BoardSignal& s, bool selected, const std::string& spinner, bool pinned) {
  std::string name = s.series.meta.title.empty() ? s.id : s.series.meta.title;
  if (name.size() > 18) name = name.substr(0, 17) + "…";

  Element spark = text(std::string(12, ' '));  // placeholder while loading
  if (s.status == Status::Loaded && !s.series.observations.empty())
    spark = text(sparkline(s.series.observations, 12)) | color(Color::Cyan);

  std::string valStr;
  Element delta = text("      ");
  if (s.status == Status::Failed) {
    valStr = "   —  ";
  } else if (const auto* o = s.series.latest()) {
    valStr = padLeft(formatValue(o->value), 7);
    auto [arrow, col] = deltaOf(s.series);
    delta = text(padLeft(arrow, 6)) | color(col);
  } else {
    valStr = "   " + spinner + "   ";  // loading (no data yet)
  }

  Element row = hbox({text(pinned ? "◆" : " ") | color(Color::Magenta),
                      text(padRight(name, 18)), text(" "), spark, text(" "),
                      text(valStr), text(" "), delta});
  if (selected) return row | inverted | focus;
  return row;
}

Element windowTabs(Window w) {
  auto tab = [&](const char* label, Window v) {
    Element t = text(label);
    return w == v ? (t | inverted) : (t | dim);
  };
  return hbox({tab(" 1Y ", Window::Y1), text(" "), tab(" 5Y ", Window::Y5), text(" "),
               tab(" MAX ", Window::MAX)});
}

// Line chart via FTXUI Canvas: auto-sizes to the available box (no manual
// dimensions), draws the series in cyan with a yellow vertical crosshair at the
// cursor. Canvas uses braille internally (2x4 sub-cells) and supports per-point
// colour, so the crosshair is a real coloured line.
Element chartElement(const std::vector<Observation>& obs, int cursor, Box& chartBox) {
  if (obs.empty()) return text("(no data in window)") | dim | center;
  double mn = obs.front().value, mx = obs.front().value;
  for (const auto& o : obs) {
    mn = std::min(mn, o.value);
    mx = std::max(mx, o.value);
  }

  auto draw = [obs, mn, mx, cursor](Canvas& c) {
    const int W = c.width(), H = c.height();
    if (W <= 0 || H <= 0 || obs.empty()) return;
    const int n = static_cast<int>(obs.size());
    const double range = (mx - mn) > 0 ? (mx - mn) : 1.0;
    auto px = [&](int i) { return n <= 1 ? 0 : static_cast<int>(std::lround(static_cast<double>(i) / (n - 1) * (W - 1))); };
    auto py = [&](double v) {
      return std::clamp(static_cast<int>(std::lround((1.0 - (v - mn) / range) * (H - 1))), 0, H - 1);
    };
    for (int i = 0; i + 1 < n; ++i)
      c.DrawPointLine(px(i), py(obs[i].value), px(i + 1), py(obs[i + 1].value), Color::Cyan);

    const int cx = px(std::clamp(cursor, 0, n - 1));  // crosshair
    c.DrawPointLine(cx, 0, cx, H - 1, Color::Yellow);

    // Peak / trough markers (drawn last so they stay visible).
    int iMax = 0, iMin = 0;
    for (int i = 1; i < n; ++i) {
      if (obs[i].value > obs[iMax].value) iMax = i;
      if (obs[i].value < obs[iMin].value) iMin = i;
    }
    c.DrawPointCircle(px(iMax), py(obs[iMax].value), 1, Color::Green);
    c.DrawPointCircle(px(iMin), py(obs[iMin].value), 1, Color::Red);
  };

  // y gutter (max at top, min at bottom) + plot; date axis below.
  Element plot = hbox({
                     vbox({text(formatValue(mx)) | dim, filler(), text(formatValue(mn)) | dim}) |
                         size(WIDTH, EQUAL, 9),
                     canvas(draw) | flex | reflect(chartBox),
                 }) |
                 flex;
  Element axis = hbox({text(std::string(9, ' ')), text(obs.front().date) | dim, filler(),
                       text(obs.back().date) | dim});
  return vbox({plot, axis});
}

// The crosshair value readout: date + value at the cursor, the change from the
// cursor to the latest point (Δ and %), and the latest value.
Element readout(const std::vector<Observation>& obs, int cursor, const std::string& unitName) {
  if (obs.empty()) return text("");
  const int c = std::clamp(cursor, 0, static_cast<int>(obs.size()) - 1);
  const auto& cur = obs[c];
  const auto& last = obs.back();
  const std::string unit = unitName.empty() ? "" : (" " + unitName);

  Element deltaEl = text("");
  if (c != static_cast<int>(obs.size()) - 1) {  // cursor isn't already the latest
    const double d = last.value - cur.value;
    const double pct = cur.value != 0.0 ? d / cur.value * 100.0 : 0.0;
    const char* arrow = d > 0 ? "▲" : d < 0 ? "▼" : "▬";
    const Color col = d > 0 ? Color::Green : d < 0 ? Color::Red : Color::GrayLight;
    char pbuf[16];
    std::snprintf(pbuf, sizeof pbuf, "%+.1f%%", pct);
    deltaEl = text("   " + std::string(arrow) + " " + formatValue(std::abs(d)) + " (" + pbuf +
                   ") → latest") |
              color(col);
  }

  return hbox({
      text("▸ ") | color(Color::Yellow),
      text(cur.date + "  ") | color(Color::Yellow),
      text(formatValue(cur.value) + unit) | bold | color(Color::Yellow),
      deltaEl,
      text("      latest " + last.date + "  " + formatValue(last.value)) | dim,
  });
}

// Deep-link to the FRED graph tool, carrying the window via cosd so the page
// opens at the same start date. (The plain /series/ page ignores cosd.)
std::string openUrl(const BoardSignal& s, Window win) {
  const std::string id = s.series.meta.id.empty() ? s.id : s.series.meta.id;
  std::string url = "https://fred.stlouisfed.org/graph/?id=" + id;
  if (win != Window::MAX && !s.series.observations.empty()) {
    const std::string cutoff = windowCutoff(s.series.observations.back().date, yearsOf(win));
    if (!cutoff.empty()) url += "&cosd=" + cutoff;
  }
  return url;
}

// Robust one-line stats for the charted window: median, σ, MAD (outlier-
// resistant scale), annualised return vol, and an outlier count.
Element statsLine(const std::vector<Observation>& wobs, const std::string& freq) {
  if (wobs.size() < 2) return text("");
  const auto v = values(wobs);
  const auto s = summarize(v);
  const double vol = volatility(wobs, periodsPerYear(freq)) * 100.0;
  const int nOut = static_cast<int>(outliers(v).size());
  std::string line = "  med " + formatValue(s.median) + "   σ " + formatValue(s.stdev) +
                     "   MAD " + formatValue(s.mad);
  char vbuf[24];
  std::snprintf(vbuf, sizeof vbuf, "   vol %.1f%%", vol);
  line += vbuf;
  Element out = text(line) | dim;
  if (nOut > 0)
    return hbox({out, text("   ⚠ " + std::to_string(nOut) + (nOut > 1 ? " outliers" : " outlier")) |
                          color(Color::Yellow)});
  return out;
}

Element detailPane(const BoardSignal& s, Window win, const std::vector<Observation>& wobs,
                   int cursor, Transform transform, Box& chartBox) {
  const auto& m = s.series.meta;
  if (s.status == Status::Loading) {
    return vbox({text(s.id) | bold, separator(), text("Loading…") | dim});
  }
  if (s.status == Status::Failed) {
    return vbox({text(s.id) | bold, separator(),
                 text("Data unavailable — press o to open on FRED") | dim});
  }

  // YoY/% change re-unit the series to percent; MA keeps the native unit.
  const bool pctUnit = transform == Transform::YoY || transform == Transform::Pct;
  const std::string dispUnit = pctUnit ? "%" : m.unit;

  std::string meta = m.frequency;
  if (!dispUnit.empty()) meta += " · " + dispUnit;
  if (!m.seasonalAdj.empty()) meta += " · " + m.seasonalAdj;

  Element lens = transform == Transform::None
                     ? text("")
                     : text("  [" + std::string(transformLabel(transform)) + "]") |
                           color(Color::Magenta) | bold;

  return vbox({
      hbox({text(m.title.empty() ? s.id : m.title) | bold, lens}),
      hbox({text(m.source + " · " + (m.id.empty() ? s.id : m.id)) | color(Color::Yellow),
            text("   " + meta) | dim}),
      windowTabs(win),
      separator(),
      chartElement(wobs, cursor, chartBox) | flex,
      separator(),
      readout(wobs, cursor, dispUnit),
      statsLine(wobs, m.frequency),
      text(openUrl(s, win)) | dim,
  });
}

// --- compare mode: two series overlaid, indexed to 100 at the window start so
// different units share one axis. A = selected (cyan), B = pinned (magenta). ---
Element compareChart(const Aligned& al, int cursor, Box& chartBox) {
  const int n = al.n();
  if (n < 2) return text("(need ≥2 overlapping dates to compare)") | dim | center;
  const double ba = al.a[0] != 0.0 ? al.a[0] : 1.0;
  const double bb = al.b[0] != 0.0 ? al.b[0] : 1.0;
  std::vector<double> na(n), nb(n);
  double mn = na[0] = al.a[0] / ba * 100.0, mx = mn;
  for (int i = 0; i < n; ++i) {
    na[i] = al.a[i] / ba * 100.0;
    nb[i] = al.b[i] / bb * 100.0;
    mn = std::min({mn, na[i], nb[i]});
    mx = std::max({mx, na[i], nb[i]});
  }
  auto draw = [na, nb, mn, mx, cursor, n](Canvas& c) {
    const int W = c.width(), H = c.height();
    if (W <= 0 || H <= 0) return;
    const double range = (mx - mn) > 0 ? (mx - mn) : 1.0;
    auto px = [&](int i) { return n <= 1 ? 0 : static_cast<int>(std::lround(static_cast<double>(i) / (n - 1) * (W - 1))); };
    auto py = [&](double v) { return std::clamp(static_cast<int>(std::lround((1.0 - (v - mn) / range) * (H - 1))), 0, H - 1); };
    for (int i = 0; i + 1 < n; ++i) {
      c.DrawPointLine(px(i), py(na[i]), px(i + 1), py(na[i + 1]), Color::Cyan);
      c.DrawPointLine(px(i), py(nb[i]), px(i + 1), py(nb[i + 1]), Color::Magenta);
    }
    const int cx = px(std::clamp(cursor, 0, n - 1));
    c.DrawPointLine(cx, 0, cx, H - 1, Color::Yellow);
  };
  Element plot = hbox({
                     vbox({text(formatValue(mx)) | dim, filler(), text(formatValue(mn)) | dim}) |
                         size(WIDTH, EQUAL, 9),
                     canvas(draw) | flex | reflect(chartBox),
                 }) |
                 flex;
  Element axis = hbox({text(std::string(9, ' ')), text(al.dates.front()) | dim, filler(),
                       text(al.dates.back()) | dim});
  return vbox({plot, axis});
}

Element compareReadout(const Aligned& al, int cursor, const std::string& aName,
                       const std::string& bName, const CompareStats& st) {
  if (al.n() == 0) return text("");
  const int c = std::clamp(cursor, 0, al.n() - 1);
  const double a = al.a[c], b = al.b[c];
  const double tsBeta = theilSen(al).slope;  // outlier-resistant β
  const int rw = std::min(12, al.n());       // rolling ρ over up to 12 periods
  const auto rc = rollingCorrelation(al, rw);
  char sbuf[128];
  std::snprintf(sbuf, sizeof sbuf, "ρ %.2f   β %.2f (robust %.2f)   n=%d%s", st.correlation, st.beta,
                tsBeta, st.n,
                rc.empty() ? "" : ("   ρ" + std::to_string(rw) + " " + formatValue(rc.back().value)).c_str());
  const std::string ratioS = b != 0.0 ? formatValue(a / b) : "—";
  return vbox({
      hbox({text("▸ " + al.dates[c] + "  ") | color(Color::Yellow),
            text(aName + " ") | color(Color::Cyan), text(formatValue(a)) | color(Color::Cyan) | bold,
            text("   " + bName + " ") | color(Color::Magenta),
            text(formatValue(b)) | color(Color::Magenta) | bold,
            text("   A−B " + formatValue(a - b)) | dim, text("   A/B " + ratioS) | dim}),
      text(sbuf) | dim,
  });
}

Element comparePane(const BoardSignal& A, const BoardSignal& B, Window win, const Aligned& al,
                    int cursor, Box& chartBox, const CompareStats& st) {
  auto shortTitle = [](const BoardSignal& s) {
    std::string t = s.series.meta.title.empty() ? s.id : s.series.meta.title;
    return t.size() > 24 ? t.substr(0, 23) + "…" : t;
  };
  const std::string aName = A.series.meta.id.empty() ? A.id : A.series.meta.id;
  const std::string bName = B.series.meta.id.empty() ? B.id : B.series.meta.id;
  return vbox({
      hbox({text("Compare  ") | bold, text(aName) | color(Color::Cyan) | bold,
            text(" vs ") | dim, text(bName) | color(Color::Magenta) | bold}),
      hbox({text(shortTitle(A)) | color(Color::Cyan) | dim, text("  vs  ") | dim,
            text(shortTitle(B)) | color(Color::Magenta) | dim}),
      windowTabs(win),
      separator(),
      compareChart(al, cursor, chartBox) | flex,
      text("  indexed to 100 at window start") | dim,
      separator(),
      compareReadout(al, cursor, aName, bName, st),
      text("  c unpin · select another signal to change A") | dim,
  });
}

// Open a url in the OS browser. Reject only chars that are dangerous inside a
// double-quoted shell argument — so '&' in a query string is fine (it's quoted).
void openInBrowser(const std::string& url) {
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return;
  if (url.find_first_of("\"'`$\\ \n\r\t") != std::string::npos) return;
#if defined(_WIN32)
  const std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
  const std::string cmd = "open \"" + url + "\" >/dev/null 2>&1";
#else
  const std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1";
#endif
  std::system(cmd.c_str());
}

}  // namespace

int runBoard(const std::string& watchlistPath, const std::string& apiKey) {
  const auto items = loadWatchlist(watchlistPath);
  if (items.empty()) {
    std::cerr << "No signals in " << watchlistPath
              << " (one FRED id per line; \"# Group\" lines start a group).\n";
    return 1;
  }

  // `signals` is written by background fetch threads and read by the UI thread,
  // so it (and the search state) live behind `mu`. UI-only state (selection,
  // window, cursor, query…) stays on the UI thread and needs no lock.
  std::mutex mu;
  std::vector<BoardSignal> signals;
  signals.reserve(items.size());
  for (const auto& it : items) {
    BoardSignal bs;
    bs.group = it.first;
    bs.id = it.second;
    signals.push_back(std::move(bs));  // status defaults to Loading
  }

  auto screen = ScreenInteractive::Fullscreen();
  // Declared after `screen` so the futures join (workers finish) while `screen`
  // and `signals` are still alive.
  std::vector<std::future<void>> tasks;
  Datawire sdk = Datawire::fred(apiKey);  // FRED source + JSON store, behind the facade
  std::atomic<int> inflight{0};  // # of fetches running (drives the spinner/ticker)

  // Worker body: fetch one series, cache it, and store the result under the lock.
  auto fetchInto = [&](int idx) {
    ++inflight;
    std::string id;
    {
      std::lock_guard<std::mutex> lk(mu);
      id = signals[idx].id;
    }
    try {
      Series s = sdk.fetch(id);  // fetch + persist, via the facade
      std::lock_guard<std::mutex> lk(mu);
      signals[idx].series = std::move(s);
      signals[idx].status = Status::Loaded;
    } catch (const std::exception&) {
      std::lock_guard<std::mutex> lk(mu);
      if (signals[idx].status != Status::Loaded)
        signals[idx].status = Status::Failed;  // keep cached data on a failed refresh
    }
    --inflight;
    screen.PostEvent(Event::Custom);  // ask the UI thread to redraw
  };
  auto refreshAll = [&] {  // force re-fetch all; current data stays visible meanwhile
    const int m = static_cast<int>(signals.size());
    for (int i = 0; i < m; ++i)
      tasks.push_back(std::async(std::launch::async, [&, i] { fetchInto(i); }));
  };

  // Initial load: show cached series instantly, only hit the network for stale ones.
  {
    const long long TTL = 6 * 3600;  // seconds
    const int m = static_cast<int>(signals.size());
    for (int i = 0; i < m; ++i) {
      auto cached = sdk.cached(signals[i].id);
      bool stale = true;
      if (cached) {
        std::lock_guard<std::mutex> lk(mu);
        signals[i].series = std::move(cached->series);
        signals[i].status = Status::Loaded;
        stale = cached->ageSec > TTL;
      }
      if (stale)
        tasks.push_back(std::async(std::launch::async, [&, i] { fetchInto(i); }));
    }
  }

  int selected = 0;
  Window win = Window::Y1;
  int cursor = 0;
  bool resetCursor = true;  // snap the crosshair to the latest point on load/switch
  bool moversMode = false;
  int compareIdx = -1;  // pinned "signal B" for compare mode; -1 = off
  Transform transform = Transform::None;
  Box chartBox;    // canvas screen box (for click-to-scrub), captured each render
  int chartN = 0;  // windowed point count, for mapping a click x -> index

  // --- in-TUI search + add -------------------------------------------------
  std::string query;
  std::vector<SearchResult> results;
  int resultSel = 0;
  std::string searchStatus;
  bool searchMode = false;
  bool helpOpen = false;

  auto runSearch = [&] {
    if (query.find_first_not_of(" \t") == std::string::npos) return;
    {
      std::lock_guard<std::mutex> lk(mu);
      searchStatus = "Searching…";
      results.clear();
    }
    resultSel = 0;
    tasks.push_back(std::async(std::launch::async, [&, q = query] {
      try {
        auto rs = sdk.search(q);
        std::lock_guard<std::mutex> lk(mu);
        results = std::move(rs);
        searchStatus = results.empty() ? "No results." : "";
      } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> lk(mu);
        results.clear();
        searchStatus = std::string("Error: ") + ex.what();
      }
      screen.PostEvent(Event::Custom);
    }));
  };
  auto addSelected = [&] {
    std::string id;
    {
      std::lock_guard<std::mutex> lk(mu);
      if (resultSel < 0 || resultSel >= static_cast<int>(results.size())) return;
      id = results[resultSel].id;
    }
    addToWatchlist(watchlistPath, id, "Added");
    int idx = -1;
    bool needFetch = false;
    {
      std::lock_guard<std::mutex> lk(mu);
      for (int i = 0; i < static_cast<int>(signals.size()); ++i)
        if (signals[i].id == id) { idx = i; break; }
      if (idx < 0) {
        BoardSignal bs;
        bs.group = "Added";
        bs.id = id;
        signals.push_back(std::move(bs));
        idx = static_cast<int>(signals.size()) - 1;
        needFetch = true;
      }
    }
    selected = idx;
    resetCursor = true;
    searchMode = false;
    if (needFetch)
      tasks.push_back(std::async(std::launch::async, [&, idx] { fetchInto(idx); }));
  };

  // Search modal as a plain Element (own text field + result list), overlaid on
  // the board with dbox. Handled in the board's single event handler — no extra
  // focusable component, so the board never loses keyboard events.
  auto buildModal = [&]() -> Element {
    Elements list;
    if (!searchStatus.empty()) {
      list.push_back(text(searchStatus) | dim);
    } else if (results.empty()) {
      list.push_back(text("Type a query and press ↵") | dim);
    } else {
      int i = 0;
      for (const auto& r : results) {
        Element line = hbox({text(padRight(r.id, 13)),
                             text(r.frequency + "·" + r.unit + "  ") | dim, text(r.title)});
        if (i == resultSel) line = line | inverted | focus;
        list.push_back(line);
        ++i;
      }
    }
    return vbox({
               text("Add signal") | bold,
               separator(),
               text("⌕ " + query + "▏"),
               separator(),
               vbox(std::move(list)) | vscroll_indicator | yframe | flex,
               separator(),
               text("↵ search · ↑↓ pick · ⇥ add · esc close") | dim,
           }) |
           border | size(WIDTH, EQUAL, 74) | size(HEIGHT, EQUAL, 24) | bgcolor(Color::Black) |
           clear_under;
  };

  auto buildHelp = [&]() -> Element {
    auto row = [](const char* k, const char* d) {
      return hbox({text(k) | color(Color::Yellow) | size(WIDTH, EQUAL, 14), text(d) | dim});
    };
    return vbox({
               text("datawire — keys") | bold,
               separator(),
               text("Board") | dim,
               row("j / k  ↑ ↓", "select signal"),
               row("h / l  ← →", "move chart crosshair"),
               row("w", "cycle window (1Y / 5Y / MAX)"),
               row("t", "lens: YoY % / % chg / 1y MA"),
               row("c", "compare: pin/overlay vs another signal"),
               row("m", "sort by biggest movers"),
               row("r", "refresh all data"),
               row("o", "open series on FRED"),
               text(""),
               text("Add signal") | dim,
               row("/  or  a", "open FRED search"),
               row("↵", "search"),
               row("↑ ↓", "pick a result"),
               row("⇥ Tab", "add selected"),
               row("esc", "close search"),
               text(""),
               text("General") | dim,
               row("?", "this help"),
               row("q / esc", "quit"),
               separator(),
               text("press any key to close") | dim | center,
           }) |
           border | size(WIDTH, EQUAL, 52) | bgcolor(Color::Black) | clear_under;
  };

  const auto startTime = std::chrono::steady_clock::now();

  auto component = Renderer([&] {
    std::lock_guard<std::mutex> lk(mu);  // signals/results read under the lock
    const int n = static_cast<int>(signals.size());
    if (selected >= n) selected = n - 1;
    if (selected < 0) selected = 0;

    static const char* SPIN[10] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - startTime)
                        .count();
    const std::string spinner = SPIN[(ms / 90) % 10];

    Elements left;
    const auto order = displayOrder(signals, moversMode);
    std::string cur = "\x01";  // sentinel so the first real group prints
    for (int oi = 0; oi < n; ++oi) {
      const int i = order[oi];
      if (!moversMode && signals[i].group != cur) {  // groups only in watchlist order
        cur = signals[i].group;
        if (!cur.empty()) left.push_back(text(" " + cur) | bold | dim);
      }
      left.push_back(signalRow(signals[i], i == selected, spinner, i == compareIdx));
    }
    Element leftPane = vbox(std::move(left)) | vscroll_indicator | yframe;

    const bool compareOn = compareIdx >= 0 && compareIdx < n && compareIdx != selected &&
                           signals[selected].status == Status::Loaded &&
                           signals[compareIdx].status == Status::Loaded;
    Element detail;
    if (compareOn) {
      const auto& A = signals[selected];
      const auto& B = signals[compareIdx];
      auto trim = [&](const BoardSignal& s) {
        return windowFilter(
            applyTransform(s.series.observations, transform, s.series.meta.frequency), yearsOf(win));
      };
      const Aligned al = align(trim(A), trim(B));
      chartN = al.n();
      if (resetCursor && chartN > 0) { cursor = chartN - 1; resetCursor = false; }
      if (chartN > 0) cursor = std::clamp(cursor, 0, chartN - 1);
      detail = comparePane(A, B, win, al, cursor, chartBox, compareStats(al));
    } else {
      const auto& sig = signals[selected];
      const auto wobs =
          sig.status == Status::Loaded
              ? windowFilter(applyTransform(sig.series.observations, transform, sig.series.meta.frequency),
                             yearsOf(win))
              : std::vector<Observation>{};
      chartN = static_cast<int>(wobs.size());
      // Snap to the latest only once data has arrived (may still be loading).
      if (resetCursor && !wobs.empty()) { cursor = chartN - 1; resetCursor = false; }
      if (!wobs.empty()) cursor = std::clamp(cursor, 0, chartN - 1);
      detail = detailPane(sig, win, wobs, cursor, transform, chartBox);
    }

    std::string status = inflight.load() > 0 ? ("  " + spinner + " refreshing")
                                             : ("  " + std::to_string(n) + " signals");
    if (moversMode) status += " · movers";
    if (compareOn) status += " · compare";
    Element header = hbox({text(" datawire ") | bold | inverted, text(status) | dim});
    Element footer = text(" j/k · h/l cursor · w window · t lens · c compare · m movers · / add · "
                          "r refresh · o open · ? help · q quit ") |
                     dim;

    Element boardEl = vbox({header, separator(),
                            hbox({leftPane | size(WIDTH, EQUAL, 48), separator(), detail | flex}) | flex,
                            separator(), footer}) |
                      border;
    if (searchMode) return dbox({boardEl, buildModal() | center});
    if (helpOpen) return dbox({boardEl, buildHelp() | center});
    return boardEl;
  });

  component |= CatchEvent([&](Event e) {
    const int n = static_cast<int>(signals.size());
    if (helpOpen) {
      if (!e.is_mouse()) helpOpen = false;  // a keypress closes help; ignore mouse
      return true;
    }
    // Modal captures all keys while open (manual text field).
    if (searchMode) {
      if (e == Event::Escape) { searchMode = false; return true; }
      if (e == Event::Return) { runSearch(); return true; }
      if (e == Event::Tab) { addSelected(); return true; }
      if (e == Event::ArrowUp) { if (resultSel > 0) --resultSel; return true; }
      if (e == Event::ArrowDown) {
        std::lock_guard<std::mutex> lk(mu);
        if (resultSel + 1 < static_cast<int>(results.size())) ++resultSel;
        return true;
      }
      if (e == Event::Backspace) { if (!query.empty()) query.pop_back(); return true; }
      if (e.is_character()) { query += e.character(); return true; }
      return true;
    }
    if (e == Event::Character('q') || e == Event::Escape) { screen.Exit(); return true; }
    if (e == Event::Character('?')) { helpOpen = true; return true; }
    if (e == Event::Character('/') || e == Event::Character('a')) {
      searchMode = true;
      query.clear();
      results.clear();
      searchStatus.clear();
      resultSel = 0;
      return true;
    }
    if (e == Event::Character('m')) { moversMode = !moversMode; return true; }
    if (e == Event::Character('j') || e == Event::ArrowDown) {
      std::lock_guard<std::mutex> lk(mu);
      const auto order = displayOrder(signals, moversMode);
      int pos = 0;
      for (int k = 0; k < n; ++k) if (order[k] == selected) { pos = k; break; }
      if (pos < n - 1) selected = order[pos + 1];
      resetCursor = true;
      return true;
    }
    if (e == Event::Character('k') || e == Event::ArrowUp) {
      std::lock_guard<std::mutex> lk(mu);
      const auto order = displayOrder(signals, moversMode);
      int pos = 0;
      for (int k = 0; k < n; ++k) if (order[k] == selected) { pos = k; break; }
      if (pos > 0) selected = order[pos - 1];
      resetCursor = true;
      return true;
    }
    if (e == Event::Character('w')) { win = nextWindow(win); resetCursor = true; return true; }
    if (e == Event::Character('t')) { transform = nextTransform(transform); resetCursor = true; return true; }
    if (e == Event::Character('c')) {  // pin current as compare B; press again on it to unpin
      compareIdx = (compareIdx == selected) ? -1 : selected;
      resetCursor = true;
      return true;
    }
    if (e == Event::Character('r')) { refreshAll(); return true; }
    if (e == Event::Character('h') || e == Event::ArrowLeft) { --cursor; return true; }
    if (e == Event::Character('l') || e == Event::ArrowRight) { ++cursor; return true; }
    if (e.is_mouse()) {  // click or drag on the chart moves the crosshair
      const auto& mm = e.mouse();
      if (mm.button == Mouse::Left && chartN > 0 && chartBox.Contain(mm.x, mm.y)) {
        const int w = std::max(1, chartBox.x_max - chartBox.x_min);
        const double frac = static_cast<double>(mm.x - chartBox.x_min) / w;
        cursor = std::clamp(static_cast<int>(std::lround(frac * (chartN - 1))), 0, chartN - 1);
        return true;
      }
      return false;
    }
    if (e == Event::Character('o')) {
      std::string url;
      {
        std::lock_guard<std::mutex> lk(mu);
        if (selected >= 0 && selected < static_cast<int>(signals.size()))
          url = openUrl(signals[selected], win);
      }
      openInBrowser(url);
      return true;
    }
    return false;
  });

  // Animation clock: repaint at ~11fps ONLY while a fetch is in flight (drives
  // the spinner smoothly); silent/event-driven when idle.
  std::atomic<bool> tickerRunning{true};
  std::thread ticker([&] {
    while (tickerRunning.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(90));
      if (inflight.load() > 0) screen.PostEvent(Event::Custom);
    }
  });

  screen.Loop(component);
  tickerRunning.store(false);
  ticker.join();
  return 0;
}

}  // namespace datawire
