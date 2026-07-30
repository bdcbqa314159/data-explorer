#pragma once
#include "secret_store.hpp"

namespace datawire {

#if defined(__APPLE__)
// macOS Keychain backend (Security.framework), generic-password items under
// service "datawire", one per secret name. Only declared on Apple platforms.
class KeychainSecretStore : public SecretStore {
public:
  std::optional<std::string> get(const std::string& name) override;
  void set(const std::string& name, const std::string& value) override;
  void remove(const std::string& name) override;
  std::string location() const override { return "macOS Keychain (service=datawire)"; }
};
#endif

}  // namespace datawire
