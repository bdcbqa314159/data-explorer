#include "feed.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

namespace feedwire {

namespace {

// Portable UTC tm -> time_t. The one platform #ifdef in the project: POSIX has
// timegm, Windows spells it _mkgmtime.
std::time_t timegmPortable(std::tm* tm) {
#if defined(_WIN32)
  return _mkgmtime(tm);
#else
  return timegm(tm);
#endif
}

// Parse RSS (RFC 822) or Atom (RFC 3339) timestamps.
// ponytail: timezone offsets are ignored — all times treated as UTC. Good enough
// to order stories that are minutes/hours apart; revisit if exact TZ matters.
std::chrono::system_clock::time_point parseDate(const std::string& raw) {
  static constexpr std::array<const char*, 2> formats = {
      "%a, %d %b %Y %H:%M:%S",  // RFC 822, e.g. "Mon, 21 Jul 2026 14:30:00 GMT"
      "%Y-%m-%dT%H:%M:%S",      // RFC 3339, e.g. "2026-07-21T14:30:00Z"
  };
  for (const char* fmt : formats) {
    std::tm tm{};
    std::istringstream ss(raw);
    ss >> std::get_time(&tm, fmt);
    if (!ss.fail()) {
      const std::time_t t = timegmPortable(&tm);
      if (t != static_cast<std::time_t>(-1)) {
        return std::chrono::system_clock::from_time_t(t);
      }
    }
  }
  return {};  // epoch -> sorts last
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
  size_t p = 0;
  while ((p = s.find(from, p)) != std::string::npos) {
    s.replace(p, from.size(), to);
    p += to.size();
  }
}

NewsItem makeItem(std::string title, std::string summary, std::string url,
                  const std::string& source,
                  std::chrono::system_clock::time_point published) {
  NewsItem it;
  it.title = std::move(title);
  it.summary = std::move(summary);
  it.url = std::move(url);
  it.source = source;
  it.published = published;
  it.sentiment = inferSentiment(it.title + " " + it.summary);
  return it;
}

}  // namespace

std::string stripHtml(const std::string& in) {
  static const std::regex cdata(R"(<!\[CDATA\[([\s\S]*?)\]\]>)");
  static const std::regex tags(R"(<[^>]+>)");
  static const std::regex whitespace(R"(\s+)");

  std::string s = std::regex_replace(in, cdata, "$1");
  s = std::regex_replace(s, tags, " ");
  replaceAll(s, "&amp;", "&");
  replaceAll(s, "&nbsp;", " ");
  replaceAll(s, "&quot;", "\"");
  replaceAll(s, "&#39;", "'");
  replaceAll(s, "&lt;", "<");
  replaceAll(s, "&gt;", ">");
  s = std::regex_replace(s, whitespace, " ");

  const auto begin = s.find_first_not_of(' ');
  if (begin == std::string::npos) return "";
  const auto end = s.find_last_not_of(' ');
  return s.substr(begin, end - begin + 1);
}

std::string inferSentiment(const std::string& text) {
  static const std::array<const char*, 10> positive = {
      "beat", "surge", "jump", "rally", "gain",
      "upgrade", "record", "growth", "bullish", "strong"};
  static const std::array<const char*, 10> negative = {
      "miss", "plunge", "drop", "fall", "downgrade",
      "loss", "weak", "bearish", "cut", "slump"};

  std::string lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  auto count = [&](const auto& words) {
    int n = 0;
    for (const char* w : words) {
      if (lower.find(w) != std::string::npos) ++n;
    }
    return n;
  };

  const int p = count(positive);
  const int n = count(negative);
  if (p > n) return "positive";
  if (n > p) return "negative";
  return "neutral";
}

std::vector<NewsItem> parseFeed(const std::string& xml, const std::string& source) {
  pugi::xml_document doc;
  if (!doc.load_string(xml.c_str())) return {};

  std::vector<NewsItem> out;

  // RSS 2.0: /rss/channel/item, <link> holds the url as text.
  for (const auto& node : doc.select_nodes("/rss/channel/item")) {
    const pugi::xml_node n = node.node();
    std::string title = stripHtml(n.child_value("title"));
    std::string url = n.child_value("link");
    if (title.empty() || url.empty()) continue;
    out.push_back(makeItem(std::move(title), stripHtml(n.child_value("description")),
                           std::move(url), source, parseDate(n.child_value("pubDate"))));
  }

  // Atom: /feed/entry, url is <link href="..."> (prefer rel="alternate").
  for (const auto& node : doc.select_nodes("/feed/entry")) {
    const pugi::xml_node n = node.node();
    std::string title = stripHtml(n.child_value("title"));
    std::string url = n.find_child_by_attribute("link", "rel", "alternate")
                          .attribute("href").value();
    if (url.empty()) url = n.child("link").attribute("href").value();
    if (title.empty() || url.empty()) continue;

    auto published = parseDate(n.child_value("updated"));
    if (published == std::chrono::system_clock::time_point{}) {
      published = parseDate(n.child_value("published"));
    }
    out.push_back(makeItem(std::move(title), stripHtml(n.child_value("summary")),
                           std::move(url), source, published));
  }

  return out;
}

}  // namespace feedwire
