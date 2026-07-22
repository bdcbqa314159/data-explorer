#pragma once
#include <filesystem>
#include <string>
#include <unordered_set>

namespace feedwire {

// Persistent set of "already read" item urls, one per line on disk. Loaded in
// the constructor; call save() to write back (no-op if nothing changed).
class ReadStore {
public:
  explicit ReadStore(std::filesystem::path path = ".cache/read.txt");

  bool isRead(const std::string& url) const;
  void markRead(const std::string& url);  // in-memory; persisted by save()
  void save() const;

private:
  std::filesystem::path path_;
  std::unordered_set<std::string> read_;
  mutable bool dirty_ = false;
};

}  // namespace feedwire
