#pragma once
#include "series.hpp"

#include <optional>
#include <string>

namespace datawire {

struct CachedSeries {
  Series series;
  long long ageSec;  // seconds since it was fetched
};

// Disk cache of fetched series (one JSON file per id) so warm launches never
// touch the network. Best-effort: any filesystem/parse error degrades to a miss.
class SeriesCache {
public:
  SeriesCache();
  std::optional<CachedSeries> get(const std::string& id) const;
  void put(const std::string& id, const Series& series) const;

private:
  std::string pathFor(const std::string& id) const;
  std::string dir_;
};

}  // namespace datawire
