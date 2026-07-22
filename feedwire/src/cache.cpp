#include "cache.hpp"

#include <fstream>
#include <functional>
#include <sstream>

namespace feedwire {

namespace {
long long nowEpochSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}  // namespace

FeedCache::FeedCache(std::filesystem::path dir) : dir_(std::move(dir)) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);  // ignore: get/put degrade gracefully
}

std::filesystem::path FeedCache::pathFor(const std::string& url) const {
  std::ostringstream name;
  name << "feed_" << std::hex << std::hash<std::string>{}(url) << ".txt";
  return dir_ / name.str();
}

std::optional<FeedCache::Entry> FeedCache::read(const std::string& url) const {
  std::ifstream in(pathFor(url), std::ios::binary);
  if (!in) return std::nullopt;

  std::string header;
  if (!std::getline(in, header)) return std::nullopt;
  long long epoch = 0;
  try {
    epoch = std::stoll(header);
  } catch (...) {
    return std::nullopt;  // corrupt cache file
  }

  std::ostringstream body;
  body << in.rdbuf();
  return Entry{epoch, body.str()};
}

std::optional<std::string> FeedCache::get(const std::string& url,
                                          std::chrono::seconds ttl) const {
  const auto entry = read(url);
  if (!entry) return std::nullopt;
  if (nowEpochSeconds() - entry->fetchedEpoch >= ttl.count()) return std::nullopt;  // stale
  return entry->body;
}

std::optional<std::string> FeedCache::getStale(const std::string& url) const {
  const auto entry = read(url);
  if (!entry) return std::nullopt;
  return entry->body;
}

void FeedCache::put(const std::string& url, const std::string& body) const {
  std::ofstream out(pathFor(url), std::ios::binary | std::ios::trunc);
  if (!out) return;  // best-effort
  out << nowEpochSeconds() << "\n" << body;
}

}  // namespace feedwire
