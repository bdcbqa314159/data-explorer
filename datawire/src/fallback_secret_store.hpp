#pragma once
#include "secret_store.hpp"

#include <memory>

namespace datawire {

// Composes two stores: reads try primary then fallback; writes go to primary;
// removes hit both. Lets a native keychain sit in front of the 0600 file so an
// existing file-stored key stays readable and migrates on the next `set`.
class FallbackSecretStore : public SecretStore {
public:
  FallbackSecretStore(std::unique_ptr<SecretStore> primary, std::unique_ptr<SecretStore> fallback);
  std::optional<std::string> get(const std::string& name) override;
  void set(const std::string& name, const std::string& value) override;
  void remove(const std::string& name) override;
  std::string location() const override;

private:
  std::unique_ptr<SecretStore> primary_, fallback_;
};

}  // namespace datawire
