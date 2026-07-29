#pragma once
#include "secret_store.hpp"

#include <filesystem>

namespace datawire {

// Portable fallback: `NAME=value` lines in a single owner-only (0600) file,
// in a 0700 directory outside the repo. Same on-disk format the app used
// before, so existing credentials files keep working.
class FileSecretStore : public SecretStore {
public:
  explicit FileSecretStore(std::filesystem::path path);
  std::optional<std::string> get(const std::string& name) override;
  void set(const std::string& name, const std::string& value) override;
  void remove(const std::string& name) override;
  std::string location() const override { return path_.string(); }

private:
  std::filesystem::path path_;
};

}  // namespace datawire
