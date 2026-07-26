#include "board.hpp"

#include "fred.hpp"
#include "http_client.hpp"
#include "series.hpp"
#include "window.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace datawire {

using namespace ftxui;

namespace {

struct BoardSignal {
  std::string group;
  std::string id;
  Series series;
  bool failed = false;
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

Element signalRow(const BoardSignal& s, bool selected) {
  std::string name = s.series.meta.title.empty() ? s.id : s.series.meta.title;
  if (name.size() > 22) name = name.substr(0, 21) + "…";

  std::string valStr;
  Element delta = text("      ");
  if (s.failed) {
    valStr = "   —  ";
  } else if (const auto* o = s.series.latest()) {
    valStr = padLeft(formatValue(o->value), 8);
    auto [arrow, col] = deltaOf(s.series);
    delta = text(padLeft(arrow, 6)) | color(col);
  } else {
    valStr = "   …  ";
  }

  Element row = hbox({text(" "), text(padRight(name, 22)), text(" "), text(valStr), text(" "), delta});
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
Element chartElement(const std::vector<Observation>& obs, int cursor) {
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
    const int cx = px(std::clamp(cursor, 0, n - 1));  // crosshair (drawn last, on top)
    c.DrawPointLine(cx, 0, cx, H - 1, Color::Yellow);
  };

  // y gutter (max at top, min at bottom) + plot; date axis below.
  Element plot = hbox({
                     vbox({text(formatValue(mx)) | dim, filler(), text(formatValue(mn)) | dim}) |
                         size(WIDTH, EQUAL, 9),
                     canvas(draw) | flex,
                 }) |
                 flex;
  Element axis = hbox({text(std::string(9, ' ')), text(obs.front().date) | dim, filler(),
                       text(obs.back().date) | dim});
  return vbox({plot, axis});
}

// The crosshair value readout: date + value at the cursor, plus the latest.
Element readout(const std::vector<Observation>& obs, int cursor, const SeriesMeta& m) {
  if (obs.empty()) return text("");
  const int c = std::clamp(cursor, 0, static_cast<int>(obs.size()) - 1);
  const auto& cur = obs[c];
  const auto& last = obs.back();
  const std::string unit = m.unit.empty() ? "" : (" " + m.unit);
  return hbox({
      text("▸ ") | color(Color::Yellow),
      text(cur.date + "  ") | color(Color::Yellow),
      text(formatValue(cur.value) + unit) | bold | color(Color::Yellow),
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

Element detailPane(const BoardSignal& s, Window win, const std::vector<Observation>& wobs,
                   int cursor) {
  const auto& m = s.series.meta;
  if (s.failed) {
    return vbox({text(s.id) | bold, separator(),
                 text("Data unavailable — press o to open on FRED") | dim});
  }

  std::string meta = m.frequency;
  if (!m.unit.empty()) meta += " · " + m.unit;
  if (!m.seasonalAdj.empty()) meta += " · " + m.seasonalAdj;

  return vbox({
      text(m.title.empty() ? s.id : m.title) | bold,
      hbox({text(m.source + " · " + (m.id.empty() ? s.id : m.id)) | color(Color::Yellow),
            text("   " + meta) | dim}),
      windowTabs(win),
      separator(),
      chartElement(wobs, cursor) | flex,
      separator(),
      readout(wobs, cursor, m),
      text(openUrl(s, win)) | dim,
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

  // Fetch every series in parallel before the UI opens.
  std::cerr << "Loading " << items.size() << " signals…\n";
  std::vector<std::future<Series>> futs;
  futs.reserve(items.size());
  for (const auto& it : items)
    futs.push_back(std::async(std::launch::async, [id = it.second, &apiKey] {
      return fetchSeries(id, apiKey);
    }));

  std::vector<BoardSignal> signals(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    signals[i].group = items[i].first;
    signals[i].id = items[i].second;
    try {
      signals[i].series = futs[i].get();
    } catch (const std::exception& e) {
      signals[i].failed = true;
      std::cerr << "[warn] " << items[i].second << ": " << e.what() << "\n";
    }
  }

  auto screen = ScreenInteractive::Fullscreen();
  int selected = 0;
  Window win = Window::Y1;
  int cursor = 0;
  bool resetCursor = true;  // snap the crosshair to the latest point on load/switch

  // --- in-TUI search + add -------------------------------------------------
  std::string query;
  std::vector<SearchResult> results;
  int resultSel = 0;
  std::string searchStatus;
  bool searchMode = false;

  auto runSearch = [&] {
    if (query.find_first_not_of(" \t") == std::string::npos) return;
    searchStatus = "Searching…";
    try {
      results = searchSeries(query, apiKey);  // ponytail: blocking; async later
      resultSel = 0;
      searchStatus = results.empty() ? "No results." : "";
    } catch (const std::exception& ex) {
      results.clear();
      searchStatus = std::string("Error: ") + ex.what();
    }
  };
  auto addSelected = [&] {
    if (resultSel < 0 || resultSel >= static_cast<int>(results.size())) return;
    const std::string id = results[resultSel].id;
    addToWatchlist(watchlistPath, id, "Added");
    BoardSignal bs;
    bs.group = "Added";
    bs.id = id;
    try {
      bs.series = fetchSeries(id, apiKey);
    } catch (const std::exception&) {
      bs.failed = true;
    }
    signals.push_back(std::move(bs));
    selected = static_cast<int>(signals.size()) - 1;
    resetCursor = true;
    searchMode = false;
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

  auto component = Renderer([&] {
    const int n = static_cast<int>(signals.size());
    Elements left;
    std::string cur = "\x01";  // sentinel so the first real group prints
    for (int i = 0; i < n; ++i) {
      if (signals[i].group != cur) {
        cur = signals[i].group;
        if (!cur.empty()) left.push_back(text(" " + cur) | bold | dim);
      }
      left.push_back(signalRow(signals[i], i == selected));
    }
    Element leftPane = vbox(std::move(left)) | vscroll_indicator | yframe;

    const auto& sig = signals[selected];
    const auto wobs =
        sig.failed ? std::vector<Observation>{} : windowFilter(sig.series.observations, yearsOf(win));
    if (resetCursor) {
      cursor = wobs.empty() ? 0 : static_cast<int>(wobs.size()) - 1;
      resetCursor = false;
    }
    if (!wobs.empty()) cursor = std::clamp(cursor, 0, static_cast<int>(wobs.size()) - 1);
    Element detail = detailPane(sig, win, wobs, cursor);

    Element header = hbox({text(" datawire ") | bold | inverted,
                           text("  " + std::to_string(n) + " signals") | dim});
    Element footer =
        text(" j/k signal · h/l cursor · w window · / add · o open · q quit ") | dim;

    Element boardEl = vbox({header, separator(),
                            hbox({leftPane | size(WIDTH, EQUAL, 40), separator(), detail | flex}) | flex,
                            separator(), footer}) |
                      border;
    if (searchMode) return dbox({boardEl, buildModal() | center});
    return boardEl;
  });

  component |= CatchEvent([&](Event e) {
    const int n = static_cast<int>(signals.size());
    // Modal captures all keys while open (manual text field).
    if (searchMode) {
      if (e == Event::Escape) { searchMode = false; return true; }
      if (e == Event::Return) { runSearch(); return true; }
      if (e == Event::Tab) { addSelected(); return true; }
      if (e == Event::ArrowUp) { if (resultSel > 0) --resultSel; return true; }
      if (e == Event::ArrowDown) {
        if (resultSel + 1 < static_cast<int>(results.size())) ++resultSel;
        return true;
      }
      if (e == Event::Backspace) { if (!query.empty()) query.pop_back(); return true; }
      if (e.is_character()) { query += e.character(); return true; }
      return true;
    }
    if (e == Event::Character('q') || e == Event::Escape) { screen.Exit(); return true; }
    if (e == Event::Character('/') || e == Event::Character('a')) {
      searchMode = true;
      query.clear();
      results.clear();
      searchStatus.clear();
      resultSel = 0;
      return true;
    }
    if (e == Event::Character('j') || e == Event::ArrowDown) {
      if (selected < n - 1) ++selected;
      resetCursor = true;
      return true;
    }
    if (e == Event::Character('k') || e == Event::ArrowUp) {
      if (selected > 0) --selected;
      resetCursor = true;
      return true;
    }
    if (e == Event::Character('w')) { win = nextWindow(win); resetCursor = true; return true; }
    if (e == Event::Character('h') || e == Event::ArrowLeft) { --cursor; return true; }
    if (e == Event::Character('l') || e == Event::ArrowRight) { ++cursor; return true; }
    if (e == Event::Character('o')) {
      openInBrowser(openUrl(signals[selected], win));
      return true;
    }
    return false;
  });

  screen.Loop(component);
  return 0;
}

}  // namespace datawire
