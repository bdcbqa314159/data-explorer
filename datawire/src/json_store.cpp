#include "json_store.hpp"

#include <utility>

namespace datawire {

std::optional<StoredSeries> JsonStore::get(const std::string& id) {
  auto c = cache_.get(id);
  if (!c) return std::nullopt;
  StoredSeries st{std::move(c->series), c->ageSec};
  st.series.origin = Origin::LocalDb;  // served from the local JSON cache
  return st;
}

void JsonStore::put(const std::string& id, const Series& series) {
  cache_.put(id, series);
}

}  // namespace datawire
