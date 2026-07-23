#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace datawire {

// ~/.config/datawire/credentials  (Windows: %APPDATA%\datawire\credentials).
// Outside the repo, so it can't be committed by accident.
std::filesystem::path credentialsPath();

// Resolve the FRED API key: env FRED_API_KEY wins, else the credentials file.
std::optional<std::string> loadApiKey();

// Write the key to the credentials file with owner-only (0600) permissions.
void saveApiKey(const std::string& key);

}  // namespace datawire
