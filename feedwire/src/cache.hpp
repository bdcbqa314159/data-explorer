#pragma once
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace feedwire {

// Disk cache of raw feed bodies with a TTL. Best-effort: any filesystem error
// degrades to "no cache" instead of throwing, so caching never breaks a run.
//
// On-disk format, one file per feed url: first line is the fetch time (unix
// epoch seconds), the rest is the verbatim body.
class FeedCache {
public:
  explicit FeedCache(std::filesystem::path dir = ".cache");

  // Cached body if present AND younger than ttl; else nullopt.
  std::optional<std::string> get(const std::string& url, std::chrono::seconds ttl) const;
  // Cached body ignoring age — offline fallback when the network fails.
  std::optional<std::string> getStale(const std::string& url) const;
  void put(const std::string& url, const std::string& body) const;

private:
  struct Entry {
    long long fetchedEpoch;
    std::string body;
  };
  std::optional<Entry> read(const std::string& url) const;
  std::filesystem::path pathFor(const std::string& url) const;

  std::filesystem::path dir_;
};

}  // namespace feedwire
