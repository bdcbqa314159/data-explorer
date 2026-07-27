#pragma once
#include "series.hpp"

#include <string>
#include <vector>

namespace datawire {

// A time-series data source (FRED today; Eurostat/etc. later). Clients and the
// Datawire facade depend on this interface, not on any specific portal.
class Source {
public:
  virtual ~Source() = default;
  virtual Series fetchSeries(const std::string& id) = 0;
  virtual std::vector<SearchResult> search(const std::string& text) = 0;
};

}  // namespace datawire
