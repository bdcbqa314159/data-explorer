// SecretStore backends: file round-trip + fallback composition. No keychain.
#include "fallback_secret_store.hpp"
#include "file_secret_store.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>

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

  // --- Fallback composition: read primary then fallback; write only primary ---
  {
    const fs::path base = fs::temp_directory_path() / "datawire_secret_test_fb";
    fs::remove_all(base, ec);
    const fs::path pp = base / "primary", fp = base / "fallback";
    FileSecretStore(fp).set("K", "old");  // seed the fallback only

    FallbackSecretStore fb(std::make_unique<FileSecretStore>(pp),
                           std::make_unique<FileSecretStore>(fp));
    assert(fb.get("K").value() == "old");  // served from fallback
    fb.set("K", "new");                    // migrates to primary
    assert(fb.get("K").value() == "new");  // primary now wins
    assert(FileSecretStore(pp).get("K").value() == "new");
    assert(FileSecretStore(fp).get("K").value() == "old");  // fallback untouched by set

    fb.remove("K");                        // remove clears both
    assert(!FileSecretStore(pp).get("K").has_value());
    assert(!FileSecretStore(fp).get("K").has_value());
    fs::remove_all(base, ec);
  }

  std::puts("secret_store_test OK");
  return 0;
}
