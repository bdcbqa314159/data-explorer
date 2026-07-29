// FileSecretStore round-trip against a temp file. No network, no keychain.
#include "file_secret_store.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>

using namespace datawire;
namespace fs = std::filesystem;

int main() {
  const fs::path p = fs::temp_directory_path() / "datawire_secret_test" / "credentials";
  std::error_code ec;
  fs::remove_all(p.parent_path(), ec);  // clean slate

  FileSecretStore s(p);
  assert(!s.get("FRED_API_KEY").has_value());  // empty

  s.set("FRED_API_KEY", "abc123");
  assert(s.get("FRED_API_KEY").value() == "abc123");

  s.set("FRED_API_KEY", "xyz789");  // overwrite, not append
  assert(s.get("FRED_API_KEY").value() == "xyz789");

  s.set("OTHER", "v");  // second key coexists
  assert(s.get("OTHER").value() == "v");
  assert(s.get("FRED_API_KEY").value() == "xyz789");

  s.remove("FRED_API_KEY");
  assert(!s.get("FRED_API_KEY").has_value());
  assert(s.get("OTHER").value() == "v");  // remove is surgical

#ifndef _WIN32
  // File must be owner-only (0600): no group/other bits.
  const auto perms = fs::status(p).permissions();
  assert((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);
#endif

  fs::remove_all(p.parent_path(), ec);
  std::puts("secret_store_test OK");
  return 0;
}
