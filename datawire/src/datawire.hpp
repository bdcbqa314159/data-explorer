#pragma once
#include "source.hpp"
#include "store.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace datawire {

// The SDK facade: a Source (where data comes from) + a Store (where it's kept).
// Clients get an offline-capable view (`cached`) plus an online refresh (`fetch`
// persists), and search. Swap the Source or Store and clients don't change.
class Datawire {
public:
  static Datawire fred(std::string apiKey);  // default wiring: FRED + JSON store
  Datawire(std::unique_ptr<Source> source, std::unique_ptr<Store> store);

  std::optional<StoredSeries> cached(const std::string& id);  // instant/offline
  Series fetch(const std::string& id);                        // online; persists. throws on error
  std::vector<SearchResult> search(const std::string& text);

private:
  std::unique_ptr<Source> source_;
  std::unique_ptr<Store> store_;
};

}  // namespace datawire
