#include "article.hpp"

#include "feed.hpp"  // stripHtml

#include <algorithm>
#include <regex>
#include <unordered_set>

namespace feedwire {

// Heuristic, same shape as the reference implementation: narrow to <article>/<main>
// if present, drop obvious non-content tags, then collect heading/paragraph/list
// blocks that are long enough to be real prose. ponytail: regex over HTML has a
// known ceiling (breaks on exotic markup) — upgrade to gumbo/lexbor if it matters.
std::vector<std::string> extractArticle(const std::string& htmlIn) {
  // Cap input so std::regex backtracking stays bounded on huge pages.
  const std::string html = htmlIn.size() > 800000 ? htmlIn.substr(0, 800000) : htmlIn;

  try {
    static const std::regex scopeRe(R"(<(article|main)\b[^>]*>([\s\S]*?)</\1>)",
                                    std::regex::icase);
    std::smatch m;
    std::string scope = std::regex_search(html, m, scopeRe) ? m[2].str() : html;

    static const std::regex noiseRe(
        R"(<(script|style|noscript|svg|form|iframe|header|footer|nav|aside)\b[^>]*>[\s\S]*?</\1>)",
        std::regex::icase);
    scope = std::regex_replace(scope, noiseRe, " ");

    static const std::regex blockRe(R"(<(h1|h2|h3|p|li)\b[^>]*>([\s\S]*?)</\1>)",
                                    std::regex::icase);
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (auto it = std::sregex_iterator(scope.begin(), scope.end(), blockRe);
         it != std::sregex_iterator(); ++it) {
      std::string text = stripHtml((*it)[2].str());
      const size_t words = static_cast<size_t>(std::count(text.begin(), text.end(), ' ')) + 1;
      if (text.size() < 40 && words < 8) continue;  // skip nav bits, bylines, etc.
      if (seen.insert(text).second) out.push_back(std::move(text));
      if (out.size() >= 40) break;
    }
    return out;
  } catch (const std::regex_error&) {
    return {};
  }
}

}  // namespace feedwire
