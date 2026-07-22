#include "tui.hpp"

#include "read_store.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace feedwire {

using namespace ftxui;

namespace {

constexpr size_t kTitleWidth = 30;   // marquee window for the selected row
constexpr long kMarqueeMsPerChar = 260;  // scroll step; higher = slower/smoother

std::string relTime(std::chrono::system_clock::time_point tp) {
  using namespace std::chrono;
  if (tp == system_clock::time_point{}) return "?";
  auto mins = duration_cast<minutes>(system_clock::now() - tp).count();
  if (mins < 0) mins = 0;
  if (mins < 60) return std::to_string(mins) + "m";
  return std::to_string(mins / 60) + "h";
}

std::string padLeft(std::string s, size_t w) {
  if (s.size() < w) s.insert(0, w - s.size(), ' ');
  return s;
}
std::string padRight(std::string s, size_t w) {
  if (s.size() > w) s.resize(w);
  else s.append(w - s.size(), ' ');
  return s;
}

// Horizontal ticker: window of `width` chars into `s`, scrolled by `offset`.
// Short strings return as-is. ponytail: byte-indexed, so we only scroll pure
// ASCII — multibyte titles clip statically to avoid splitting a codepoint.
std::string marquee(const std::string& s, size_t width, size_t offset) {
  if (s.size() <= width) return s;
  for (unsigned char c : s)
    if (c >= 0x80) return s.substr(0, width);
  const std::string padded = s + "    ";  // gap before it loops
  const size_t start = offset % padded.size();
  std::string out;
  out.reserve(width);
  for (size_t i = 0; i < width; ++i) out += padded[(start + i) % padded.size()];
  return out;
}

Color sentimentColor(const std::string& s) {
  if (s == "positive") return Color::Green;
  if (s == "negative") return Color::Red;
  return Color::GrayLight;
}

// One list row: [dot] [ 12m] [CNBC      ] title-start… — aligned columns.
// The selected row marquees its title; others show the start, clipped.
Element rowElement(const NewsItem& it, bool unread, bool focused, size_t offset) {
  Element dot = text(unread ? "● " : "  ");
  Element time = text(padLeft(relTime(it.published), 4) + " ");
  Element src = text(padRight(it.source, 10) + " ");

  if (focused) {
    Element title = text(marquee(it.title, kTitleWidth, offset));
    return hbox({dot, time, src, title | flex}) | inverted;
  }
  dot = dot | color(unread ? Color::Yellow : Color::GrayDark);
  time = time | dim;
  src = src | color(Color::Cyan);
  Element title = unread ? (text(it.title) | bold) : (text(it.title) | dim);
  return hbox({dot, time, src, title | flex});
}

Element detailElement(const NewsItem& it) {
  return vbox({
      text(it.title) | bold,
      separator(),
      hbox({
          text(it.source) | color(Color::Yellow),
          text("   "),
          text(relTime(it.published) + " ago") | dim,
          text("   "),
          text(it.sentiment) | color(sentimentColor(it.sentiment)),
      }),
      separator(),
      paragraph(it.summary.empty() ? "(no summary)" : it.summary) | flex,
      separator(),
      text(it.url) | dim,
      text("↑/↓ or j/k  ·  o open in browser  ·  q quit") | dim,
  });
}

// Open a url in the OS browser. Best-effort; refuses anything that isn't a plain
// http(s) url so we never hand shell metacharacters to std::system.
// ponytail: shell-out is the simplest cross-platform "open"; the guard is the ceiling.
void openInBrowser(const std::string& url) {
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return;
  if (url.find_first_of("\"'`$;|&<>\\ \n\r\t") != std::string::npos) return;
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

void runTui(std::vector<NewsItem>& stories, ReadStore& readStore) {
  if (stories.empty()) return;

  auto screen = ScreenInteractive::Fullscreen();
  int selected = 0;
  const int count = static_cast<int>(stories.size());
  size_t marqueeOffset = 0;

  // One MenuEntry per story; each row styles itself from its item + focus + tick.
  auto list = Container::Vertical({}, &selected);
  for (int i = 0; i < count; ++i) {
    MenuEntryOption opt;
    opt.transform = [&stories, &readStore, &marqueeOffset, i](const EntryState& s) {
      const NewsItem& it = stories[i];
      return rowElement(it, !readStore.isRead(it.url), s.focused, marqueeOffset);
    };
    list->Add(MenuEntry("", opt));
  }

  const auto startTime = std::chrono::steady_clock::now();
  int lastSelected = -1;  // -1 so the initially-selected row is marked read

  auto renderer = Renderer(list, [&] {
    using namespace std::chrono;
    const auto elapsed = duration_cast<milliseconds>(steady_clock::now() - startTime).count();
    marqueeOffset = static_cast<size_t>(elapsed / kMarqueeMsPerChar);

    if (selected != lastSelected) {
      lastSelected = selected;
      readStore.markRead(stories[selected].url);  // browsing = reading
    }
    int unread = 0;
    for (const auto& s : stories) unread += readStore.isRead(s.url) ? 0 : 1;

    Element header = hbox({
        text(" feedwire ") | bold | inverted,
        text("  " + std::to_string(count) + " stories") | dim,
        text("  ·  " + std::to_string(unread) + " unread") | dim,
    });

    return vbox({
        header,
        separator(),
        hbox({
            list->Render() | vscroll_indicator | frame | size(WIDTH, EQUAL, 52),
            separator(),
            detailElement(stories[selected]) | flex,
        }) | flex,
    }) | border;
  });

  renderer |= CatchEvent([&](Event e) {
    if (e == Event::Character('q') || e == Event::Escape) {
      screen.Exit();
      return true;
    }
    if (e == Event::Character('o')) {
      openInBrowser(stories[selected].url);
      return true;
    }
    if (e == Event::Character('j')) {
      if (selected < count - 1) ++selected;
      return true;
    }
    if (e == Event::Character('k')) {
      if (selected > 0) --selected;
      return true;
    }
    return false;
  });

  // Nudge a redraw a few times a second so the selected row's title animates.
  std::atomic<bool> running{true};
  std::thread ticker([&] {
    while (running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(120));
      screen.PostEvent(Event::Custom);
    }
  });

  screen.Loop(renderer);
  running.store(false);
  ticker.join();
  readStore.save();
}

}  // namespace feedwire
