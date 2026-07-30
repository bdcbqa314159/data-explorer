// datawire-server — M6 slice 2: exercise the MySQL-backed store (Db) with a
// full series round-trip (meta + observations). Becomes the HTTP server in
// slice 3; this self-test then moves to its own target.

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
      std::cerr << "datawire-server: store round-trip FAILED\n";
      return 1;
    }

    // Re-upsert with fewer observations to prove wholesale replacement.
    s.observations = {{"2026-08-01", 4.4}};
    db.upsertSeries(s);
    const auto again = db.getSeries("PROBE");
    if (!again || again->observations.size() != 1) {
      std::cerr << "datawire-server: replace-on-upsert FAILED\n";
      return 1;
    }

    std::cout << "datawire-server: store round-trip OK — " << got->meta.title << ", "
              << got->observations.size() << " obs (last=" << got->observations.back().value
              << "), replace→" << again->observations.size() << " obs\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n(is MySQL running? `brew services start mysql`)\n";
    return 1;
  }
}
