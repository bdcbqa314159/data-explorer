#include "fred.hpp"
#include "http_client.hpp"

namespace datawire {

Series fetchSeries(const std::string& id, const std::string& apiKey) {
  Series s;
  s.meta = parseSeriesMeta(httpGet(fredSeriesUrl(id, apiKey)));
  s.observations = parseObservations(httpGet(fredObservationsUrl(id, apiKey)));
  if (s.meta.id.empty()) {  // metadata call failed but keep something usable
    s.meta.id = id;
    s.meta.sourceUrl = fredSeriesPage(id);
  }
  return s;
}

std::vector<SearchResult> searchSeries(const std::string& text, const std::string& apiKey) {
  return parseSearch(httpGet(fredSearchUrl(text, apiKey)));
}

}  // namespace datawire
