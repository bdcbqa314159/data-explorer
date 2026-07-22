#include "feed.hpp"
#include "http_client.hpp"

#include <algorithm>
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

struct FeedConfig {
  std::string name;
  std::string url;
};

// feeds.txt format: "Name|URL" per line. Lines starting with # are comments.
// A line with no '|' is treated as URL-only (name = url).
std::vector<FeedConfig> loadFeeds(const std::string& path) {
  std::vector<FeedConfig> feeds;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    const auto begin = line.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) continue;  // blank
    line = line.substr(begin);
    if (line.front() == '#') continue;         // comment

    const auto bar = line.find('|');
    if (bar == std::string::npos) {
      feeds.push_back({line, line});
    } else {
      feeds.push_back({line.substr(0, bar), line.substr(bar + 1)});
    }
  }
  return feeds;
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
  const std::string configPath = argc > 1 ? argv[1] : "feeds.txt";

  CurlGlobal curlGlobal;  // one-time init, torn down at scope exit

  const auto feeds = loadFeeds(configPath);
  if (feeds.empty()) {
    std::cerr << "No feeds found in " << configPath << "\n";
    return 1;
  }

  // Fetch + parse every feed concurrently — one task per feed.
  std::vector<std::future<std::vector<NewsItem>>> tasks;
  tasks.reserve(feeds.size());
  for (const auto& feed : feeds) {
    tasks.push_back(std::async(std::launch::async, [feed]() -> std::vector<NewsItem> {
      try {
        return parseFeed(httpGet(feed.url), feed.name);
      } catch (const std::exception& e) {
        std::cerr << "[warn] " << feed.name << ": " << e.what() << "\n";
        return {};  // one dead feed does not sink the batch
      }
    }));
  }

  std::vector<NewsItem> all;
  for (auto& task : tasks) {
    auto items = task.get();
    all.insert(all.end(), std::make_move_iterator(items.begin()),
               std::make_move_iterator(items.end()));
  }

  // Dedup by url, keeping first seen.
  std::unordered_set<std::string> seen;
  std::vector<NewsItem> merged;
  merged.reserve(all.size());
  for (auto& item : all) {
    if (seen.insert(item.url).second) merged.push_back(std::move(item));
  }

  // Newest first.
  std::sort(merged.begin(), merged.end(),
            [](const NewsItem& a, const NewsItem& b) { return a.published > b.published; });

  for (const auto& item : merged) {
    std::cout << std::right << std::setw(4) << relativeTime(item.published) << "  "
              << std::left << std::setw(12) << item.source.substr(0, 12) << "  "
              << item.title << "\n";
  }
  std::cerr << "\n" << merged.size() << " stories from " << feeds.size() << " feeds\n";
  return 0;
}
