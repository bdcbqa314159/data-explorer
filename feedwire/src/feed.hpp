#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace feedwire {

struct NewsItem {
  std::string title;
  std::string summary;
  std::string url;
  std::string source;   // feed name, e.g. "CNBC"
  std::chrono::system_clock::time_point published{};  // epoch if unparseable
  std::string sentiment;  // "positive" | "negative" | "neutral"
};

// Parse an RSS 2.0 or Atom feed. Never throws; returns {} on unparseable input.
// Items missing a title or a url are dropped.
std::vector<NewsItem> parseFeed(const std::string& xml, const std::string& source);

// --- helpers, exposed for testing ---------------------------------------
std::string stripHtml(const std::string& in);
std::string inferSentiment(const std::string& text);

}  // namespace feedwire
