#include "credentials.hpp"

#include "fallback_secret_store.hpp"
#include "file_secret_store.hpp"
#include "keychain_secret_store.hpp"
#include "secret_store.hpp"

#include <cstdlib>
#include <utility>

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
  auto file = std::make_unique<FileSecretStore>(credentialsPath());
#if defined(__APPLE__)
  // Keychain in front; the 0600 file stays readable so an existing key still
  // works and migrates to the Keychain on the next `set`.
  return std::make_unique<FallbackSecretStore>(std::make_unique<KeychainSecretStore>(),
                                               std::move(file));
#else
  // TODO(M4 slice 3): Windows Credential Manager / Linux libsecret in front here.
  return file;
#endif
}

std::optional<std::string> loadApiKey() {
  // Env var wins — the escape hatch for CI/scripting.
  if (const char* env = std::getenv(kApiKeyName); env && *env) return std::string(env);
  return defaultSecretStore()->get(kApiKeyName);
}

void saveApiKey(const std::string& key) { defaultSecretStore()->set(kApiKeyName, key); }

}  // namespace datawire
