#include "fred_source.hpp"

#include "fred.hpp"

#include <utility>

namespace datawire {

FredSource::FredSource(std::string apiKey) : apiKey_(std::move(apiKey)) {}

Series FredSource::fetchSeries(const std::string& id) {
  return ::datawire::fetchSeries(id, apiKey_);  // free function in fred.hpp
}

std::vector<SearchResult> FredSource::search(const std::string& text) {
  return searchSeries(text, apiKey_);
}

}  // namespace datawire
