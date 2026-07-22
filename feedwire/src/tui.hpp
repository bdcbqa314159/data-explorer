#pragma once
#include <vector>

#include "feed.hpp"

namespace feedwire {

class ReadStore;

// Launch the interactive two-pane reader over `stories`. Browsing an item marks
// it read; state is saved to `readStore` on exit. Requires a real terminal.
void runTui(std::vector<NewsItem>& stories, ReadStore& readStore);

}  // namespace feedwire
