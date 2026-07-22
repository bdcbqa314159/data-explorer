// Plain-assert tests for the persistence layer. Run with: ctest --preset default
#include "cache.hpp"
#include "read_store.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace feedwire;
namespace fs = std::filesystem;

int main() {
  const fs::path tmp = fs::temp_directory_path() / "feedwire_store_test";
  fs::remove_all(tmp);

  // --- FeedCache: put/get, TTL freshness, stale fallback -----------------
  FeedCache cache(tmp);
  const std::string url = "https://example.com/feed";

  assert(!cache.get(url, std::chrono::seconds(60)).has_value());  // nothing yet

  cache.put(url, "hello body");
  const auto fresh = cache.get(url, std::chrono::seconds(60));
  assert(fresh.has_value() && *fresh == "hello body");

  assert(!cache.get(url, std::chrono::seconds(0)).has_value());   // ttl 0 -> stale
  const auto stale = cache.getStale(url);                          // ...but still readable
  assert(stale.has_value() && *stale == "hello body");

  // --- ReadStore: persists across instances ------------------------------
  const fs::path readPath = tmp / "read.txt";
  {
    ReadStore rs(readPath);
    assert(!rs.isRead("u1"));
    rs.markRead("u1");
    rs.save();
  }
  {
    ReadStore rs(readPath);  // fresh load from disk
    assert(rs.isRead("u1"));
    assert(!rs.isRead("u2"));
  }

  fs::remove_all(tmp);
  std::cout << "all store tests passed\n";
  return 0;
}
