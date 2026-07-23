// Plain-assert tests for the FRED parsers. Run with: ctest --preset default
#include "fred.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace datawire;

int main() {
  // observations: skip "." (missing), parse numeric, keep order.
  const char* obs = R"({"observations":[
    {"date":"2026-04-01","value":"4.0"},
    {"date":"2026-05-01","value":"."},
    {"date":"2026-06-01","value":"4.1"}]})";
  auto o = parseObservations(obs);
  assert(o.size() == 2);
  assert(o[0].date == "2026-04-01" && o[0].value == 4.0);
  assert(o[1].date == "2026-06-01" && o[1].value == 4.1);

  // metadata: read from the "seriess"[0] object, prefer *_short units.
  const char* meta = R"({"seriess":[{
    "id":"UNRATE","title":"Unemployment Rate",
    "units":"Percent","units_short":"%",
    "frequency":"Monthly","seasonal_adjustment_short":"SA",
    "observation_end":"2026-06-01"}]})";
  auto m = parseSeriesMeta(meta);
  assert(m.id == "UNRATE");
  assert(m.title == "Unemployment Rate");
  assert(m.unit == "%");
  assert(m.asOf == "2026-06-01");
  assert(m.sourceUrl == "https://fred.stlouisfed.org/series/UNRATE");

  // search: list of hits.
  const char* search = R"({"seriess":[
    {"id":"UNRATE","title":"Unemployment Rate","units_short":"%","frequency_short":"M"},
    {"id":"U6RATE","title":"Total Unemployed plus...","units_short":"%","frequency_short":"M"}]})";
  auto r = parseSearch(search);
  assert(r.size() == 2);
  assert(r[0].id == "UNRATE" && r[0].frequency == "M");

  // malformed input -> empty, no throw.
  assert(parseObservations("not json").empty());
  assert(parseSearch("{}").empty());

  // URL building.
  assert(fredSeriesPage("UNRATE") == "https://fred.stlouisfed.org/series/UNRATE");
  assert(fredObservationsUrl("UNRATE", "KEY").find("series_id=UNRATE") != std::string::npos);
  assert(fredSearchUrl("credit card", "KEY").find("search_text=credit+card") != std::string::npos);

  std::cout << "all fred tests passed\n";
  return 0;
}
