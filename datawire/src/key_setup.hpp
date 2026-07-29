#pragma once
#include "secret_store.hpp"

namespace datawire {

// Interactive FRED API-key setup: a small TUI with a masked input that validates
// the key against FRED (live request) and only stores it in `store` on success.
// Returns 0 if a key was validated and saved, 1 if cancelled/failed.
int runKeySetup(SecretStore& store);

}  // namespace datawire
