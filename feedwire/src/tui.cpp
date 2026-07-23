#include "tui.hpp"

#include "article.hpp"
#include "cache.hpp"
#include "http_client.hpp"
#include "read_store.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace feedwire {

using namespace ftxui;

namespace {

constexpr size_t kTitleWidth = 30;        // marquee window for the selected row
constexpr long kMarqueeMsPerChar = 260;   // scroll step; higher = slower/smoother

struct ArticleState {
  enum Status { Loading, Ready, Failed } status = Loading;
  std::vector<std::string> paragraphs;
};

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
// ponytail: byte-indexed, so we only scroll pure ASCII — multibyte titles clip
// statically to avoid splitting a codepoint.
std::string marquee(const std::string& s, size_t width, size_t offset) {
  if (s.size() <= width) return s;
  for (unsigned char c : s)
    if (c >= 0x80) return s.substr(0, width);
  const std::string padded = s + "    ";
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

// Greedy word-wrap to `width` columns. ponytail: byte width, so multibyte lines
// wrap a touch short — fine for reading, upgrade with a grapheme width later.
std::vector<std::string> wrapText(const std::string& s, size_t width) {
  std::vector<std::string> lines;
  std::istringstream ss(s);
  std::string word, line;
  while (ss >> word) {
    if (line.empty()) line = word;
    else if (line.size() + 1 + word.size() <= width) line += " " + word;
    else { lines.push_back(line); line = word; }
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

// `scroll` is clamped in place against the wrapped-line count so paging stays
// bounded; `width`/`visible` come from the live terminal size.
Element detailElement(const NewsItem& it, const ArticleState& art, int& scroll,
                      int width, int visible) {
  Elements head;
  head.push_back(text(it.title) | bold);
  head.push_back(separator());
  head.push_back(hbox({
      text(it.source) | color(Color::Yellow),
      text("   "),
      text(relTime(it.published) + " ago") | dim,
      text("   "),
      text(it.sentiment) | color(sentimentColor(it.sentiment)),
  }));
  head.push_back(text(it.url) | dim);
  head.push_back(separator());

  if (art.status == ArticleState::Loading) {
    scroll = 0;
    head.push_back(text("Loading article…") | dim);
    head.push_back(text(""));
    head.push_back(paragraph(it.summary.empty() ? "(no summary)" : it.summary));
    return vbox(std::move(head)) | flex;
  }
  if (art.status == ArticleState::Failed || art.paragraphs.empty()) {
    scroll = 0;
    head.push_back(paragraph(it.summary.empty() ? "(no summary)" : it.summary));
    head.push_back(text(""));
    head.push_back(text("Full text unavailable — press o to open in browser") | dim);
    return vbox(std::move(head)) | flex;
  }

  // Flatten the article to display lines, then show a window of them.
  std::vector<std::string> lines;
  for (const auto& p : art.paragraphs) {
    for (auto& l : wrapText(p, static_cast<size_t>(std::max(20, width)))) lines.push_back(std::move(l));
    lines.push_back("");  // blank between paragraphs
  }
  const int total = static_cast<int>(lines.size());
  const int maxScroll = std::max(0, total - visible);
  scroll = std::clamp(scroll, 0, maxScroll);

  Elements body;
  body.push_back(scroll > 0 ? (text("  ↑ more above") | dim) : text(""));
  for (int i = scroll; i < std::min(total, scroll + visible); ++i)
    body.push_back(lines[i].empty() ? text(" ") : text(lines[i]));
  body.push_back(scroll < maxScroll ? (text("  ↓ more below") | dim) : text(""));

  for (auto& b : body) head.push_back(std::move(b));
  return vbox(std::move(head)) | flex;
}

// Open a url in the OS browser. Best-effort; refuses anything that isn't a plain
// http(s) url so we never hand shell metacharacters to std::system.
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
  int detailScroll = 0;
  const int count = static_cast<int>(stories.size());
  size_t marqueeOffset = 0;

  // Article read-through: fetched on a background thread, keyed by url. Declared
  // after `screen` so the futures (which capture screen for PostEvent) join
  // before screen is destroyed.
  FeedCache articleCache(".cache/articles");
  std::mutex mu;
  std::unordered_map<std::string, ArticleState> articles;
  std::atomic<bool> cancelFetch{false};  // tripped on quit to abort in-flight fetches
  std::vector<std::future<void>> tasks;
  int pageStep = 5;  // article scroll step, updated from the live pane height

  auto ensureLoading = [&](const std::string& url) {
    {
      std::lock_guard<std::mutex> lk(mu);
      if (articles.count(url)) return;  // already loading or loaded
      articles.emplace(url, ArticleState{});
    }
    tasks.push_back(std::async(std::launch::async, [&, url] {
      ArticleState st;
      try {
        std::string html;
        if (auto cached = articleCache.get(url, std::chrono::hours(6))) {
          html = *cached;
        } else {
          html = httpGet(url, 10, &cancelFetch);  // abortable on quit
          articleCache.put(url, html);
        }
        st.paragraphs = extractArticle(html);
        st.status = st.paragraphs.empty() ? ArticleState::Failed : ArticleState::Ready;
      } catch (...) {
        st.status = ArticleState::Failed;
      }
      {
        std::lock_guard<std::mutex> lk(mu);
        articles[url] = std::move(st);
      }
      screen.PostEvent(Event::Custom);  // ask the UI thread to redraw
    }));
  };

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
  int lastSelected = -1;

  auto renderer = Renderer(list, [&] {
    using namespace std::chrono;
    const auto elapsed = duration_cast<milliseconds>(steady_clock::now() - startTime).count();
    marqueeOffset = static_cast<size_t>(elapsed / kMarqueeMsPerChar);

    const NewsItem& cur = stories[selected];
    if (selected != lastSelected) {
      lastSelected = selected;
      detailScroll = 0;
      readStore.markRead(cur.url);  // browsing = reading
    }
    ensureLoading(cur.url);

    ArticleState art;
    {
      std::lock_guard<std::mutex> lk(mu);
      auto it = articles.find(cur.url);
      if (it != articles.end()) art = it->second;
    }

    int unread = 0;
    for (const auto& s : stories) unread += readStore.isRead(s.url) ? 0 : 1;

    Element header = hbox({
        text(" feedwire ") | bold | inverted,
        text("  " + std::to_string(count) + " stories") | dim,
        text("  ·  " + std::to_string(unread) + " unread") | dim,
    });
    Element footer =
        text(" ↑/↓ or j/k list  ·  space/b scroll article  ·  o open  ·  q quit ") | dim;

    // Approximate the detail pane from the live terminal size (chrome removed).
    const int detailWidth = std::max(20, screen.dimx() - 52 - 4);
    const int detailVisible = std::max(3, screen.dimy() - 12);
    pageStep = std::max(1, detailVisible - 2);

    return vbox({
               header,
               separator(),
               hbox({
                   list->Render() | vscroll_indicator | frame | size(WIDTH, EQUAL, 52),
                   separator(),
                   detailElement(cur, art, detailScroll, detailWidth, detailVisible) | flex,
               }) | flex,
               separator(),
               footer,
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
    if (e == Event::Character('j')) {
      if (selected < count - 1) ++selected;
      return true;
    }
    if (e == Event::Character('k')) {
      if (selected > 0) --selected;
      return true;
    }
    if (e == Event::Character(' ')) {  // page article down
      detailScroll += pageStep;
      return true;
    }
    if (e == Event::Character('b')) {  // page article up
      detailScroll = std::max(0, detailScroll - pageStep);
      return true;
    }
    return false;
  });

  // Nudge redraws so the selected row's title animates.
  std::atomic<bool> running{true};
  std::thread ticker([&] {
    while (running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(120));
      screen.PostEvent(Event::Custom);
    }
  });

  screen.Loop(renderer);
  cancelFetch.store(true);  // abort any in-flight article fetch so futures join fast
  running.store(false);
  ticker.join();
  readStore.save();
}

}  // namespace feedwire
