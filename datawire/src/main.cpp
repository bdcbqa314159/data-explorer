#include "fred.hpp"
#include "http_client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace datawire;

// Phase 0 CLI probe (the two-pane TUI comes next). Verifies the FRED adapter
// end to end:
//   datawire UNRATE              -> one series' latest value + count
//   datawire search unemployment -> catalog search results
int main(int argc, char** argv) {
  const char* key = std::getenv("FRED_API_KEY");
  if (!key || !*key) {
    std::cerr << "Set FRED_API_KEY (free: https://fredaccount.stlouisfed.org/apikeys)\n";
    return 1;
  }

  CurlGlobal curlGlobal;
  try {
    if (argc > 2 && std::string(argv[1]) == "search") {
      for (const auto& r : searchSeries(argv[2], key)) {
        std::cout << r.id << "\t" << r.frequency << "\t" << r.unit << "\t" << r.title << "\n";
      }
      return 0;
    }

    const std::string id = argc > 1 ? argv[1] : "UNRATE";
    const Series s = fetchSeries(id, key);
    std::cout << s.meta.title << "  (" << s.meta.id << ", " << s.meta.frequency
              << ", " << s.meta.unit << ")\n";
    if (const auto* o = s.latest()) {
      std::cout << "latest: " << o->value << " on " << o->date << "\n";
    }
    std::cout << s.observations.size() << " observations · " << s.meta.sourceUrl << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
