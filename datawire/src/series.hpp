#pragma once
#include <string>
#include <vector>

namespace datawire {

// The common model every source normalises to (anti-corruption layer): the UI,
// cache, and future web layer never know which portal a number came from.

struct Observation {
  std::string date;  // ISO YYYY-MM-DD
  double value;
};

struct SeriesMeta {
  std::string id;          // e.g. "UNRATE"
  std::string title;       // "Unemployment Rate"
  std::string unit;        // "%"
  std::string frequency;   // "Monthly" / "M"
  std::string seasonalAdj; // "SA"
  std::string asOf;        // latest observation date
  std::string sourceUrl;   // deep-link back to the portal
  std::string source = "FRED";
};

struct SearchResult {  // one catalog hit in the add-signal screen
  std::string id;
  std::string title;
  std::string unit;
  std::string frequency;
};

// Retrieval provenance of an in-memory copy — where THIS copy was produced.
// Transient (never persisted); stamped by the source/store that produced it.
// Distinct from SeriesMeta.source (the data provider, e.g. "FRED").
enum class Origin { Unknown, FredApi, LocalDb, ServerSync };

inline const char* originLabel(Origin o) {
  switch (o) {
    case Origin::FredApi: return "live";
    case Origin::LocalDb: return "cached";
    case Origin::ServerSync: return "synced";
    default: return "";
  }
}

struct Series {
  SeriesMeta meta;
  std::vector<Observation> observations;  // ascending by date
  Origin origin = Origin::Unknown;        // set on retrieval; not persisted

  const Observation* latest() const {
    return observations.empty() ? nullptr : &observations.back();
  }
};

}  // namespace datawire
