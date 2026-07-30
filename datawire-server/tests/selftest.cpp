// datawire-server store self-test — needs a running MySQL. Exercises Db with a
// full series round-trip (meta + observations) and wholesale replacement.
// Not wired into ctest (it needs the DB); run manually after `brew services start mysql`.

#include "db.hpp"

#include <iostream>

int main() {
  using namespace datawire;
  try {
    server::Db db;

    Series s;
    s.meta.id = "PROBE";
    s.meta.title = "Probe Series";
    s.meta.unit = "idx";
    s.meta.frequency = "Monthly";
    s.meta.seasonalAdj = "SA";
    s.meta.asOf = "2026-07-01";
    s.meta.sourceUrl = "https://example.org/PROBE";
    s.meta.source = "TEST";
    s.observations = {{"2026-05-01", 1.1}, {"2026-06-01", 2.2}, {"2026-07-01", 3.3}};

    db.upsertSeries(s);
    const auto got = db.getSeries("PROBE");
    if (!got || got->observations.size() != 3 || got->meta.title != "Probe Series") {
      std::cerr << "store round-trip FAILED\n";
      return 1;
    }

    s.observations = {{"2026-08-01", 4.4}};  // fewer -> proves wholesale replace
    db.upsertSeries(s);
    const auto again = db.getSeries("PROBE");
    if (!again || again->observations.size() != 1) {
      std::cerr << "replace-on-upsert FAILED\n";
      return 1;
    }

    std::cout << "selftest OK — " << got->meta.title << ", " << got->observations.size()
              << " obs (last=" << got->observations.back().value << "), replace→"
              << again->observations.size() << " obs\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n(is MySQL running? `brew services start mysql`)\n";
    return 1;
  }
}
