#include "article.hpp"

#include "feed.hpp"  // stripHtml (whitespace normalise)

#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include <algorithm>
#include <mutex>
#include <unordered_set>

namespace feedwire {

namespace {

// libxml2 global init must run once before any threaded parsing.
void ensureInit() {
  static std::once_flag once;
  std::call_once(once, [] { xmlInitParser(); });
}

// Collect text of every node matched by `xpath`, keeping only prose-length blocks.
std::vector<std::string> collect(xmlDocPtr doc, const char* xpath) {
  std::vector<std::string> out;
  xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
  if (!ctx) return out;

  xmlXPathObjectPtr res = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(xpath), ctx);
  if (res && res->nodesetval) {
    std::unordered_set<std::string> seen;
    for (int i = 0; i < res->nodesetval->nodeNr; ++i) {
      xmlChar* content = xmlNodeGetContent(res->nodesetval->nodeTab[i]);
      if (!content) continue;
      std::string text = stripHtml(reinterpret_cast<const char*>(content));  // collapse whitespace
      xmlFree(content);

      // Keep real sentences; drop nav labels / bylines / captions. Word count
      // (not byte length) so multibyte nav labels don't sneak past the filter.
      // ponytail: space-delimited, so space-less scripts (CJK) won't extract —
      // fine for these English feeds.
      const size_t words = static_cast<size_t>(std::count(text.begin(), text.end(), ' ')) + 1;
      if (words < 8) continue;
      if (seen.insert(text).second) out.push_back(std::move(text));
      if (out.size() >= 40) break;
    }
  }
  if (res) xmlXPathFreeObject(res);
  xmlXPathFreeContext(ctx);
  return out;
}

}  // namespace

std::vector<std::string> extractArticle(const std::string& html) {
  if (html.empty()) return {};
  ensureInit();

  // HTML_PARSE_RECOVER makes libxml2 tolerate broken markup like a browser does.
  htmlDocPtr doc = htmlReadMemory(
      html.c_str(), static_cast<int>(html.size()), nullptr, "utf-8",
      HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
  if (!doc) return {};

  // Prefer content inside <article>/<main>; fall back to the whole document.
  // Exclude anything inside nav/header/footer/aside/figure so sidebars, menus,
  // and captions don't leak in.
  const char* kNotChrome =
      " and not(ancestor::nav) and not(ancestor::header) and not(ancestor::footer)"
      " and not(ancestor::aside) and not(ancestor::figure)";
  const std::string scoped =
      std::string("(//article|//main)//*[(self::p or self::li)") + kNotChrome + "]";
  const std::string global = std::string("//*[(self::p or self::li)") + kNotChrome + "]";

  std::vector<std::string> out = collect(doc, scoped.c_str());
  if (out.size() < 2) out = collect(doc, global.c_str());

  xmlFreeDoc(doc);
  return out;
}

}  // namespace feedwire
