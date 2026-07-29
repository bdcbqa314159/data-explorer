#include "board.hpp"
#include "credentials.hpp"
#include "fred.hpp"
#include "http_client.hpp"
#include "secret_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace datawire;

namespace {

std::string trim(std::string s) {
  auto notspace = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
  return s;
}

std::string mask(const std::string& k) {
  if (k.size() <= 4) return "****";
  return std::string(k.size() - 4, '*') + k.substr(k.size() - 4);
}

int runKeyCommand(int argc, char** argv) {
  const std::string sub = argc > 2 ? argv[2] : "status";
  if (sub == "set") {
    std::cout << "Paste your FRED API key (stored to a 0600 file, not shell history): ";
    std::string k;
    std::getline(std::cin, k);
    k = trim(k);
    if (k.empty()) {
      std::cerr << "No key entered.\n";
      return 1;
    }
    saveApiKey(k);
    std::cout << "Saved to " << defaultSecretStore()->location() << ".\n";
    return 0;
  }
  // status
  const auto k = loadApiKey();
  std::cout << "secret store: " << defaultSecretStore()->location() << "\n";
  if (k) {
    const bool fromEnv = std::getenv("FRED_API_KEY") != nullptr;
    std::cout << "key: " << mask(*k) << (fromEnv ? "  (from FRED_API_KEY env)" : "  (from store)") << "\n";
  } else {
    std::cout << "key: not set — run `datawire key set`\n";
  }
  return 0;
}

}  // namespace

//   datawire                     -> the two-pane board (watchlist.txt)
//   datawire board [watchlist]   -> board from a specific watchlist
//   datawire key set             -> store the API key securely
//   datawire key                 -> show key status (masked)
//   datawire get UNRATE          -> probe one series' latest value + count
//   datawire search "credit ..." -> catalog search results
int main(int argc, char** argv) {
  const std::string cmd = argc > 1 ? argv[1] : "";

  if (cmd == "key") return runKeyCommand(argc, argv);

  const auto key = loadApiKey();
  if (!key) {
    std::cerr << "No FRED API key. Run `datawire key set` "
                 "(free key: https://fredaccount.stlouisfed.org/apikeys)\n";
    return 1;
  }

  CurlGlobal curlGlobal;

  if (cmd.empty() || cmd == "board") {
    const std::string wl = (cmd == "board" && argc > 2) ? argv[2] : "watchlist.txt";
    return runBoard(wl, *key);
  }

  try {
    if (cmd == "search" && argc > 2) {
      for (const auto& r : searchSeries(argv[2], *key)) {
        std::cout << r.id << "\t" << r.frequency << "\t" << r.unit << "\t" << r.title << "\n";
      }
      return 0;
    }
    if (cmd == "get" && argc > 2) {
      const Series s = fetchSeries(argv[2], *key);
      std::cout << s.meta.title << "  (" << s.meta.id << ", " << s.meta.frequency
                << ", " << s.meta.unit << ")\n";
      if (const auto* o = s.latest()) {
        std::cout << "latest: " << o->value << " on " << o->date << "\n";
      }
      std::cout << s.observations.size() << " observations · " << s.meta.sourceUrl << "\n";
      return 0;
    }
    std::cerr << "usage: datawire [board [watchlist]] | get <ID> | search <text> | key [set]\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
