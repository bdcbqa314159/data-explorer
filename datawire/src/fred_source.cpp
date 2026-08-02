#include "fred_source.hpp"

#include "fred.hpp"

#include <utility>

namespace datawire {

FredSource::FredSource(std::string apiKey) : apiKey_(std::move(apiKey)) {}

Series FredSource::fetchSeries(const std::string& id) {
  Series s = ::datawire::fetchSeries(id, apiKey_);  // free function in fred.hpp
  s.origin = Origin::FredApi;                        // fresh from the live API
  return s;
}

std::vector<SearchResult> FredSource::search(const std::string& text) {
  return searchSeries(text, apiKey_);
}

}  // namespace datawire
