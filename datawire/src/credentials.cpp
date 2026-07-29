#include "credentials.hpp"

#include "file_secret_store.hpp"
#include "secret_store.hpp"

#include <cstdlib>

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

constexpr const char* kApiKeyName = "FRED_API_KEY";

}  // namespace

fs::path credentialsPath() {
#if defined(_WIN32)
  return homeBase() / "datawire" / "credentials";
#else
  return homeBase() / ".config" / "datawire" / "credentials";
#endif
}

// Platform picker. Slice 1: the portable 0600 file. Native keychain backends
// (macOS Keychain, Windows Credential Manager, Linux Secret Service) slot in
// here in the next slices, falling back to the file when unavailable.
std::unique_ptr<SecretStore> defaultSecretStore() {
  return std::make_unique<FileSecretStore>(credentialsPath());
}

std::optional<std::string> loadApiKey() {
  // Env var wins — the escape hatch for CI/scripting.
  if (const char* env = std::getenv(kApiKeyName); env && *env) return std::string(env);
  return defaultSecretStore()->get(kApiKeyName);
}

void saveApiKey(const std::string& key) { defaultSecretStore()->set(kApiKeyName, key); }

}  // namespace datawire
