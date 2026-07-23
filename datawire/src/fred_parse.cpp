#include "fred.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <string>

namespace datawire {

namespace {

constexpr const char* kBase = "https://api.stlouisfed.org/fred/";

std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else if (c == ' ') {
      out += '+';
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0xF];
    }
  }
  return out;
}

}  // namespace

std::string fredObservationsUrl(const std::string& id, const std::string& apiKey) {
  return std::string(kBase) + "series/observations?series_id=" + urlEncode(id) +
         "&api_key=" + apiKey + "&file_type=json&sort_order=asc";
}

std::string fredSeriesUrl(const std::string& id, const std::string& apiKey) {
  return std::string(kBase) + "series?series_id=" + urlEncode(id) +
         "&api_key=" + apiKey + "&file_type=json";
}

std::string fredSearchUrl(const std::string& text, const std::string& apiKey, int limit) {
  return std::string(kBase) + "series/search?search_text=" + urlEncode(text) +
         "&api_key=" + apiKey + "&file_type=json&order_by=popularity&sort_order=desc" +
         "&limit=" + std::to_string(limit);
}

std::string fredSeriesPage(const std::string& id) {
  return "https://fred.stlouisfed.org/series/" + id;
}

std::vector<Observation> parseObservations(const std::string& json) {
  std::vector<Observation> out;
  try {
    const auto j = nlohmann::json::parse(json);
    for (const auto& o : j.at("observations")) {
      const std::string v = o.value("value", ".");
      if (v == ".") continue;  // FRED marks missing values with a dot
      try {
        out.push_back({o.value("date", std::string{}), std::stod(v)});
      } catch (...) { /* skip non-numeric */ }
    }
  } catch (...) { /* malformed -> empty */ }
  return out;
}

SeriesMeta parseSeriesMeta(const std::string& json) {
  SeriesMeta m;
  try {
    const auto j = nlohmann::json::parse(json);
    const auto& arr = j.at("seriess");  // FRED's double-s quirk
    if (!arr.empty()) {
      const auto& s = arr[0];
      m.id = s.value("id", "");
      m.title = s.value("title", "");
      m.unit = s.value("units_short", s.value("units", ""));
      m.frequency = s.value("frequency", "");
      m.seasonalAdj = s.value("seasonal_adjustment_short", "");
      m.asOf = s.value("observation_end", "");
      m.sourceUrl = fredSeriesPage(m.id);
    }
  } catch (...) { /* malformed -> empty meta */ }
  return m;
}

std::vector<SearchResult> parseSearch(const std::string& json) {
  std::vector<SearchResult> out;
  try {
    const auto j = nlohmann::json::parse(json);
    for (const auto& s : j.at("seriess")) {
      out.push_back({s.value("id", ""), s.value("title", ""),
                     s.value("units_short", s.value("units", "")),
                     s.value("frequency_short", s.value("frequency", ""))});
    }
  } catch (...) { /* malformed -> empty */ }
  return out;
}

}  // namespace datawire
