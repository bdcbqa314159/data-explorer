// SqliteStore round-trip against an in-memory DB — no file, no network.
#include "sqlite_store.hpp"

#include <cassert>
#include <cstdio>

using namespace datawire;

int main() {
  SqliteStore store(":memory:");

  assert(!store.get("UNRATE").has_value());  // empty store

  Series s;
  s.meta.id = "UNRATE";
  s.meta.title = "Unemployment Rate";
  s.meta.unit = "%";
  s.meta.frequency = "Monthly";
  s.meta.seasonalAdj = "SA";
  s.meta.asOf = "2026-06-01";
  s.meta.sourceUrl = "https://fred.stlouisfed.org/series/UNRATE";
  s.meta.source = "FRED";
  s.observations = {{"2026-04-01", 3.9}, {"2026-05-01", 4.0}, {"2026-06-01", 4.1}};
  store.put("UNRATE", s);

  auto got = store.get("UNRATE");
  assert(got.has_value());
  assert(got->series.meta.id == "UNRATE");
  assert(got->series.meta.title == "Unemployment Rate");
  assert(got->series.meta.unit == "%");
  assert(got->series.meta.sourceUrl == "https://fred.stlouisfed.org/series/UNRATE");
  assert(got->series.observations.size() == 3);
  assert(got->series.observations[0].date == "2026-04-01");   // ascending
  assert(got->series.observations[2].value == 4.1);
  assert(got->ageSec >= 0 && got->ageSec < 5);                 // just stored

  // Re-put replaces observations wholesale (not append) and refreshes meta.
  s.observations = {{"2026-07-01", 4.2}};
  store.put("UNRATE", s);
  got = store.get("UNRATE");
  assert(got->series.observations.size() == 1);
  assert(got->series.observations[0].date == "2026-07-01");

  std::puts("sqlite_store_test OK");
  return 0;
}
