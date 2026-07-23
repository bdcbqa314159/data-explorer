#include "board.hpp"

#include "fred.hpp"
#include "http_client.hpp"
#include "series.hpp"
#include "window.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
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

Element chartElement(const std::vector<Observation>& obs) {
  if (obs.empty()) return text("(no data in window)") | dim | center;
  double mn = obs.front().value, mx = obs.front().value;
  for (const auto& o : obs) {
    mn = std::min(mn, o.value);
    mx = std::max(mx, o.value);
  }
  return graph([obs, mn, mx](int w, int h) -> std::vector<int> {
    std::vector<int> out(std::max(0, w), 0);
    if (w <= 0 || h <= 0 || obs.empty()) return out;
    const double range = (mx - mn) > 0 ? (mx - mn) : 1.0;
    const int n = static_cast<int>(obs.size());
    for (int x = 0; x < w; ++x) {
      const int idx = (n <= 1) ? 0 : (w <= 1 ? n - 1 : static_cast<int>(static_cast<long long>(x) * (n - 1) / (w - 1)));
      const int y = static_cast<int>(std::lround((obs[idx].value - mn) / range * (h - 1)));
      out[x] = std::clamp(y, 0, h - 1);
    }
    return out;
  });
}

Element recentObs(const std::vector<Observation>& obs) {
  Elements rows{text("RECENT") | dim};
  int cnt = 0;
  for (auto it = obs.rbegin(); it != obs.rend() && cnt < 8; ++it, ++cnt)
    rows.push_back(hbox({text(it->date), text("   "), text(padLeft(formatValue(it->value), 10))}));
  return vbox(std::move(rows));
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

Element detailPane(const BoardSignal& s, Window win) {
  const auto& m = s.series.meta;
  if (s.failed) {
    return vbox({text(s.id) | bold, separator(),
                 text("Data unavailable — press o to open on FRED") | dim});
  }
  const auto wobs = windowFilter(s.series.observations, yearsOf(win));

  std::string meta = m.frequency;
  if (!m.unit.empty()) meta += " · " + m.unit;
  if (!m.seasonalAdj.empty()) meta += " · " + m.seasonalAdj;

  return vbox({
      text(m.title.empty() ? s.id : m.title) | bold,
      hbox({text(m.source + " · " + (m.id.empty() ? s.id : m.id)) | color(Color::Yellow),
            text("   " + meta) | dim}),
      windowTabs(win),
      separator(),
      chartElement(wobs) | flex,
      separator(),
      recentObs(wobs),
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
  const int n = static_cast<int>(signals.size());
  Window win = Window::Y1;

  auto component = Renderer([&] {
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
    Element detail = detailPane(signals[selected], win);

    Element header = hbox({text(" datawire ") | bold | inverted,
                           text("  " + std::to_string(n) + " signals") | dim});
    Element footer = text(" j/k select · w window · o open source · q quit ") | dim;

    return vbox({header, separator(),
                 hbox({leftPane | size(WIDTH, EQUAL, 40), separator(), detail | flex}) | flex,
                 separator(), footer}) |
           border;
  });

  component |= CatchEvent([&](Event e) {
    if (e == Event::Character('q') || e == Event::Escape) { screen.Exit(); return true; }
    if (e == Event::Character('j') || e == Event::ArrowDown) { if (selected < n - 1) ++selected; return true; }
    if (e == Event::Character('k') || e == Event::ArrowUp) { if (selected > 0) --selected; return true; }
    if (e == Event::Character('w')) { win = nextWindow(win); return true; }
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
