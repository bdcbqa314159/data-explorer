#include "datawire.hpp"

#include "fred_source.hpp"
#include "json_store.hpp"

#include <utility>

namespace datawire {

Datawire Datawire::fred(std::string apiKey) {
  return Datawire(std::make_unique<FredSource>(std::move(apiKey)),
                  std::make_unique<JsonStore>());
}

Datawire::Datawire(std::unique_ptr<Source> source, std::unique_ptr<Store> store)
    : source_(std::move(source)), store_(std::move(store)) {}

std::optional<StoredSeries> Datawire::cached(const std::string& id) {
  return store_->get(id);
}

Series Datawire::fetch(const std::string& id) {
  Series s = source_->fetchSeries(id);
  store_->put(id, s);
  return s;
}

std::vector<SearchResult> Datawire::search(const std::string& text) {
  return source_->search(text);
}

}  // namespace datawire
