#include "read_store.hpp"

#include <fstream>
#include <system_error>

namespace feedwire {

ReadStore::ReadStore(std::filesystem::path path) : path_(std::move(path)) {
  std::ifstream in(path_);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF
    if (!line.empty()) read_.insert(line);
  }
}

bool ReadStore::isRead(const std::string& url) const {
  return read_.find(url) != read_.end();
}

void ReadStore::markRead(const std::string& url) {
  if (read_.insert(url).second) dirty_ = true;
}

void ReadStore::save() const {
  if (!dirty_) return;
  std::error_code ec;
  if (path_.has_parent_path()) {
    std::filesystem::create_directories(path_.parent_path(), ec);
  }
  std::ofstream out(path_, std::ios::trunc);
  if (!out) return;
  for (const auto& url : read_) out << url << "\n";
  dirty_ = false;
}

}  // namespace feedwire
