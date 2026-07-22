#include "tui.hpp"

#include "read_store.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <cstdlib>
#include <string>

namespace feedwire {

using namespace ftxui;

namespace {

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

Color sentimentColor(const std::string& s) {
  if (s == "positive") return Color::Green;
  if (s == "negative") return Color::Red;
  return Color::GrayLight;
}

// One list row: [dot] [ 12m] [CNBC      ] Headline… — aligned columns.
Element rowElement(const NewsItem& it, bool unread, bool focused) {
  Element dot = text(unread ? "● " : "  ");
  Element time = text(padLeft(relTime(it.published), 4) + " ");
  Element src = text(padRight(it.source, 10) + " ");
  Element title = text(it.title);

  if (focused) {
    return hbox({dot, time, src, title | flex}) | inverted;
  }
  dot = dot | color(unread ? Color::Yellow : Color::GrayDark);
  time = time | dim;
  src = src | color(Color::Cyan);
  title = unread ? (title | bold) : (title | dim);
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

  // One MenuEntry per story; each row styles itself from its NewsItem + focus.
  auto list = Container::Vertical({}, &selected);
  for (int i = 0; i < count; ++i) {
    MenuEntryOption opt;
    opt.transform = [&stories, &readStore, i](const EntryState& s) {
      const NewsItem& it = stories[i];
      return rowElement(it, !readStore.isRead(it.url), s.focused);
    };
    list->Add(MenuEntry("", opt));
  }

  int lastSelected = -1;  // -1 so the initially-selected row is marked read
  auto renderer = Renderer(list, [&] {
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
    if (e == Event::Character('j')) {  // vim down
      if (selected < count - 1) ++selected;
      return true;
    }
    if (e == Event::Character('k')) {  // vim up
      if (selected > 0) --selected;
      return true;
    }
    return false;
  });

  screen.Loop(renderer);
  readStore.save();
}

}  // namespace feedwire
