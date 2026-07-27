#pragma once
#include "series.hpp"

#include <optional>
#include <string>

namespace datawire {

struct StoredSeries {
  Series series;
  long long ageSec;  // seconds since it was stored
};

// Persistence for fetched series (JSON files today; SQLite in M2). The facade
// reads from here for instant/offline display and writes fresh fetches back.
class Store {
public:
  virtual ~Store() = default;
  virtual std::optional<StoredSeries> get(const std::string& id) = 0;
  virtual void put(const std::string& id, const Series& series) = 0;
};

}  // namespace datawire
