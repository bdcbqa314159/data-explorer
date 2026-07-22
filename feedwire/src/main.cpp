#include "cache.hpp"
#include "feed.hpp"
#include "http_client.hpp"
#include "read_store.hpp"
#include "tui.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

using namespace feedwire;

namespace {

struct Options {
  std::string configPath = "feeds.txt";
  std::string search;
  std::string source;
  std::chrono::minutes ttl{10};
  bool unread = false;
  bool markRead = false;
  bool useCache = true;
  bool tui = false;
  bool help = false;
};

struct FeedConfig {
  std::string name;
  std::string url;
};

void printUsage() {
  std::cout <<
      "feedwire — one curated stream from many feeds\n\n"
      "Usage: feedwire [options] [config-file]\n\n"
      "  --config PATH    feed list (default: feeds.txt)\n"
      "  --search TERM    only stories whose title/summary contains TERM\n"
      "  --source NAME    only stories from this feed (case-insensitive)\n"
      "  --ttl MINUTES    cache freshness window (default: 10)\n"
      "  --no-cache       always refetch, ignore fresh cache\n"
      "  --unread         only stories not yet marked read\n"
      "  --mark-read      mark the shown stories read, then exit\n"
      "  --tui            interactive two-pane reader (arrows/j-k, o open, q quit)\n"
      "  -h, --help       this help\n\n"
      "Unread stories are prefixed with '*'.\n";
}

Options parseArgs(int argc, char** argv, bool& ok) {
  Options o;
  ok = true;
  auto value = [&](int& i) -> std::string {
    if (i + 1 >= argc) {
      std::cerr << "Missing value for " << argv[i] << "\n";
      ok = false;
      return "";
    }
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--config") o.configPath = value(i);
    else if (a == "--search") o.search = value(i);
    else if (a == "--source") o.source = value(i);
    else if (a == "--ttl") {
      const std::string v = value(i);
      try { o.ttl = std::chrono::minutes(std::stoi(v)); }
      catch (...) { std::cerr << "Bad --ttl value: " << v << "\n"; ok = false; }
    }
    else if (a == "--no-cache") o.useCache = false;
    else if (a == "--unread") o.unread = true;
    else if (a == "--mark-read") o.markRead = true;
    else if (a == "--tui") o.tui = true;
    else if (a == "-h" || a == "--help") o.help = true;
    else if (!a.empty() && a[0] != '-') o.configPath = a;  // positional config
    else { std::cerr << "Unknown option: " << a << "\n"; ok = false; }
  }
  return o;
}

// feeds.txt format: "Name|URL" per line. '#' comments, blank lines ignored.
// A line with no '|' is treated as URL-only (name = url).
std::vector<FeedConfig> loadFeeds(const std::string& path) {
  std::vector<FeedConfig> feeds;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const auto begin = line.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) continue;
    line = line.substr(begin);
    if (line.front() == '#') continue;

    const auto bar = line.find('|');
    if (bar == std::string::npos) feeds.push_back({line, line});
    else feeds.push_back({line.substr(0, bar), line.substr(bar + 1)});
  }
  return feeds;
}

// Fetch a feed, preferring fresh cache, falling back to stale cache when the
// network is down. Increments cacheHits when a cached copy is used.
std::vector<NewsItem> loadFeed(const FeedConfig& feed, const FeedCache& cache,
                               std::chrono::seconds ttl, bool useCache,
                               std::atomic<int>& cacheHits) {
  if (useCache) {
    if (auto body = cache.get(feed.url, ttl)) {
      ++cacheHits;
      return parseFeed(*body, feed.name);
    }
  }
  try {
    std::string body = httpGet(feed.url);
    cache.put(feed.url, body);
    return parseFeed(body, feed.name);
  } catch (const std::exception& e) {
    if (auto body = cache.getStale(feed.url)) {  // offline fallback
      std::cerr << "[warn] " << feed.name << ": " << e.what() << " (using cached copy)\n";
      ++cacheHits;
      return parseFeed(*body, feed.name);
    }
    std::cerr << "[warn] " << feed.name << ": " << e.what() << "\n";
    return {};
  }
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
bool containsCI(const std::string& hay, const std::string& needle) {
  return toLower(hay).find(toLower(needle)) != std::string::npos;
}
bool equalsCI(const std::string& a, const std::string& b) {
  return toLower(a) == toLower(b);
}

std::string relativeTime(std::chrono::system_clock::time_point tp) {
  using namespace std::chrono;
  if (tp == system_clock::time_point{}) return "?";
  auto mins = duration_cast<minutes>(system_clock::now() - tp).count();
  if (mins < 0) mins = 0;
  if (mins < 60) return std::to_string(mins) + "m";
  return std::to_string(mins / 60) + "h";
}

}  // namespace

int main(int argc, char** argv) {
  bool ok = true;
  const Options opt = parseArgs(argc, argv, ok);
  if (!ok) return 2;
  if (opt.help) { printUsage(); return 0; }

  CurlGlobal curlGlobal;
  const FeedCache cache;
  ReadStore readStore;

  const auto feeds = loadFeeds(opt.configPath);
  if (feeds.empty()) {
    std::cerr << "No feeds found in " << opt.configPath << "\n";
    return 1;
  }

  // Fetch every feed concurrently, cache-aware.
  std::atomic<int> cacheHits{0};
  std::vector<std::future<std::vector<NewsItem>>> tasks;
  tasks.reserve(feeds.size());
  for (const auto& feed : feeds) {
    tasks.push_back(std::async(std::launch::async, [&, feed] {
      return loadFeed(feed, cache, opt.ttl, opt.useCache, cacheHits);
    }));
  }

  std::vector<NewsItem> all;
  for (auto& task : tasks) {
    auto items = task.get();
    all.insert(all.end(), std::make_move_iterator(items.begin()),
               std::make_move_iterator(items.end()));
  }

  // Dedup by url, newest first.
  std::unordered_set<std::string> seen;
  std::vector<NewsItem> merged;
  merged.reserve(all.size());
  for (auto& item : all) {
    if (seen.insert(item.url).second) merged.push_back(std::move(item));
  }
  std::sort(merged.begin(), merged.end(),
            [](const NewsItem& a, const NewsItem& b) { return a.published > b.published; });

  // Filter: source, search, unread.
  std::vector<NewsItem> shown;
  for (auto& item : merged) {
    if (!opt.source.empty() && !equalsCI(item.source, opt.source)) continue;
    if (!opt.search.empty() && !containsCI(item.title + " " + item.summary, opt.search)) continue;
    if (opt.unread && readStore.isRead(item.url)) continue;
    shown.push_back(std::move(item));
  }

  if (opt.tui) {
    runTui(shown, readStore);  // browsing marks read and saves on exit
    return 0;
  }

  int unreadCount = 0;
  for (const auto& item : shown) {
    const bool unread = !readStore.isRead(item.url);
    if (unread) ++unreadCount;
    std::cout << (unread ? '*' : ' ') << ' '
              << std::right << std::setw(4) << relativeTime(item.published) << "  "
              << std::left << std::setw(12) << item.source.substr(0, 12) << "  "
              << item.title << "\n";
  }

  if (opt.markRead) {
    for (const auto& item : shown) readStore.markRead(item.url);
    readStore.save();
  }

  std::cerr << "\n" << shown.size() << " shown (" << unreadCount << " unread), "
            << cacheHits.load() << "/" << feeds.size() << " feeds from cache\n";
  return 0;
}
