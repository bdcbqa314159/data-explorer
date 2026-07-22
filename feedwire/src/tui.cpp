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
  if (tp == system_clock::time_point{}) return "  ?";
  auto mins = duration_cast<minutes>(system_clock::now() - tp).count();
  if (mins < 0) mins = 0;
  if (mins < 60) return std::to_string(mins) + "m";
  return std::to_string(mins / 60) + "h";
}

// One list row: "* 12m  CNBC  Headline" (no star once read).
std::string label(const NewsItem& item, bool unread) {
  std::string time = relTime(item.published);
  std::string src = item.source.substr(0, 10);
  return std::string(unread ? "* " : "  ") + time + "  " + src + "  " + item.title;
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

  std::vector<std::string> entries;
  entries.reserve(stories.size());
  for (const auto& s : stories) entries.push_back(label(s, !readStore.isRead(s.url)));

  auto markCurrent = [&] {
    NewsItem& s = stories[selected];
    if (!readStore.isRead(s.url)) {
      readStore.markRead(s.url);
      entries[selected] = label(s, /*unread=*/false);
    }
  };

  MenuOption menuOption;
  menuOption.on_change = [&] { markCurrent(); };
  auto menu = Menu(&entries, &selected, menuOption);

  auto renderer = Renderer(menu, [&] {
    const NewsItem& s = stories[selected];
    Element detail = vbox({
        text(s.title) | bold,
        separator(),
        hbox({
            text(s.source) | color(Color::Yellow),
            text("   "),
            text(relTime(s.published)) | dim,
            text("   "),
            text(s.sentiment) | dim,
        }),
        separator(),
        paragraph(s.summary.empty() ? "(no summary)" : s.summary) | flex,
        separator(),
        text("↑/↓ or j/k  ·  o open in browser  ·  q quit") | dim,
    });

    return hbox({
               menu->Render() | vscroll_indicator | frame | size(WIDTH, EQUAL, 48),
               separator(),
               detail | flex,
           }) |
           border;
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
    return false;
  });

  markCurrent();  // the first, pre-selected item counts as read
  screen.Loop(renderer);
  readStore.save();
}

}  // namespace feedwire
