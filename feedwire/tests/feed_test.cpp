// Plain-assert tests, no framework. Run with: ctest --preset default
#include "article.hpp"
#include "feed.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace feedwire;

int main() {
  // stripHtml: tags removed, entities decoded, whitespace collapsed + trimmed.
  assert(stripHtml("<b>Hello</b> &amp; bye") == "Hello & bye");
  assert(stripHtml("<![CDATA[raw <i>text</i>]]>") == "raw text");

  // sentiment: positive words win.
  assert(inferSentiment("Stocks rally on strong earnings") == "positive");
  assert(inferSentiment("Shares plunge as revenue misses") == "negative");
  assert(inferSentiment("Company holds annual meeting") == "neutral");

  // parseFeed: RSS, one valid item; the item with no link is dropped.
  const char* rss = R"(<?xml version="1.0"?>
<rss version="2.0"><channel>
  <item>
    <title>Stocks rally on strong earnings</title>
    <description>Markets &lt;b&gt;surge&lt;/b&gt; today</description>
    <link>https://example.com/a</link>
    <pubDate>Mon, 21 Jul 2026 14:30:00 GMT</pubDate>
  </item>
  <item><title>No link here</title><link></link></item>
</channel></rss>)";
  auto items = parseFeed(rss, "Test");
  assert(items.size() == 1);
  assert(items[0].title == "Stocks rally on strong earnings");
  assert(items[0].url == "https://example.com/a");
  assert(items[0].source == "Test");
  assert(items[0].sentiment == "positive");
  assert(items[0].published != std::chrono::system_clock::time_point{});

  // parseFeed: Atom, link is an attribute.
  const char* atom = R"(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <entry>
    <title>New release ships</title>
    <summary>Details inside</summary>
    <link rel="alternate" href="https://example.com/b"/>
    <updated>2026-07-21T09:00:00Z</updated>
  </entry>
</feed>)";
  auto atomItems = parseFeed(atom, "Atom");
  assert(atomItems.size() == 1);
  assert(atomItems[0].url == "https://example.com/b");

  // Garbage in -> empty out, no throw.
  assert(parseFeed("not xml at all", "X").empty());

  // --- extractArticle: keep prose, drop noise/short bits --------------------
  const char* page = R"(<html><body><article>
    <p>This is the first substantial paragraph of the article body text here.</p>
    <script>var x = 'junk should be removed';</script>
    <p>Short</p>
    <p>Second substantial paragraph with plenty of words to be kept as well.</p>
  </article></body></html>)";
  auto paras = extractArticle(page);
  assert(paras.size() == 2);  // two long <p>; short one and <script> dropped
  assert(paras[0].find("first substantial") != std::string::npos);
  assert(paras[1].find("Second substantial") != std::string::npos);
  for (const auto& p : paras) {
    assert(p.find("junk") == std::string::npos);  // script content gone
    assert(p != "Short");
  }
  assert(extractArticle("<html>no article-ish content</html>").empty());

  std::cout << "all tests passed\n";
  return 0;
}
