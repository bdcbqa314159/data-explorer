#pragma once
#include "series.hpp"

#include <string>
#include <vector>

namespace datawire {

// --- URL builders (pure) -------------------------------------------------
std::string fredObservationsUrl(const std::string& id, const std::string& apiKey);
std::string fredSeriesUrl(const std::string& id, const std::string& apiKey);
std::string fredSearchUrl(const std::string& text, const std::string& apiKey, int limit = 25);
std::string fredSeriesPage(const std::string& id);  // human deep-link

// --- Parsers (pure; tolerant — return empty on bad input) ----------------
std::vector<Observation> parseObservations(const std::string& json);
SeriesMeta parseSeriesMeta(const std::string& json);
std::vector<SearchResult> parseSearch(const std::string& json);

// --- Fetchers (network; apiKey from FRED_API_KEY) ------------------------
Series fetchSeries(const std::string& id, const std::string& apiKey);
std::vector<SearchResult> searchSeries(const std::string& text, const std::string& apiKey);

}  // namespace datawire
