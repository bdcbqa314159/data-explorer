#pragma once
#include <string>
#include <vector>

namespace feedwire {

// Extract readable article paragraphs from a raw HTML page. Never throws;
// returns {} when nothing usable is found. Best-effort heuristic, not a browser.
std::vector<std::string> extractArticle(const std::string& html);

}  // namespace feedwire
