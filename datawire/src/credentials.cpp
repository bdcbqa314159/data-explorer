#include "credentials.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace datawire {

namespace {

fs::path homeBase() {
#if defined(_WIN32)
  if (const char* p = std::getenv("APPDATA")) return fs::path(p);
  if (const char* u = std::getenv("USERPROFILE")) return fs::path(u);
#else
  if (const char* h = std::getenv("HOME")) return fs::path(h);
#endif
  return fs::current_path();
}

std::string trimTrailing(std::string v) {
  while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ' || v.back() == '\t')) {
    v.pop_back();
  }
  return v;
}

}  // namespace

fs::path credentialsPath() {
#if defined(_WIN32)
  return homeBase() / "datawire" / "credentials";
#else
  return homeBase() / ".config" / "datawire" / "credentials";
#endif
}

std::optional<std::string> loadApiKey() {
  if (const char* env = std::getenv("FRED_API_KEY"); env && *env) {
    return std::string(env);
  }
  std::ifstream in(credentialsPath());
  std::string line;
  const std::string prefix = "FRED_API_KEY=";
  while (std::getline(in, line)) {
    if (line.rfind(prefix, 0) == 0) {
      const std::string v = trimTrailing(line.substr(prefix.size()));
      if (!v.empty()) return v;
    }
  }
  return std::nullopt;
}

void saveApiKey(const std::string& key) {
  const auto path = credentialsPath();
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  // Lock down the directory too (0700 on POSIX; harmless on Windows).
  fs::permissions(path.parent_path(), fs::perms::owner_all, fs::perm_options::replace, ec);

  {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << "FRED_API_KEY=" << key << "\n";
  }
  // Owner read/write only (0600 on POSIX).
  fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace, ec);
}

}  // namespace datawire
