#pragma once
#include "secret_store.hpp"

#include <filesystem>

namespace datawire {

// Portable fallback: `NAME=value` lines in a single owner-only (0600) file,
// in a 0700 directory outside the repo. Same on-disk format the app used
// before, so existing credentials files keep working.
//
// SECURITY CEILING: values are stored in PLAINTEXT — 0600 is access control,
// not encryption. Anyone who can read the file (backup, disk image, same-user
// process) reads the secret. Real at-rest encryption (passphrase → Argon2i →
// XChaCha20-Poly1305, e.g. vendored Monocypher) is deferred to the private-
// data phase. This store is only reached when NO OS keychain is available; on
// macOS the Keychain backend is used instead and this file is never written.
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
