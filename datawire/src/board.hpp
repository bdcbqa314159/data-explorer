#pragma once
#include <string>

namespace datawire {

// Load a watchlist, fetch each series from FRED (in parallel), and run the
// two-pane board TUI. Returns a process exit code.
int runBoard(const std::string& watchlistPath, const std::string& apiKey);

}  // namespace datawire
