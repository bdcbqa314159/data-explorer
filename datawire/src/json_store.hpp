#pragma once
#include "cache.hpp"
#include "store.hpp"

namespace datawire {

// Store backed by the per-series JSON cache. (SQLite replaces this in M2 behind
// the same interface.)
class JsonStore : public Store {
public:
  std::optional<StoredSeries> get(const std::string& id) override;
  void put(const std::string& id, const Series& series) override;

private:
  SeriesCache cache_;
};

}  // namespace datawire
