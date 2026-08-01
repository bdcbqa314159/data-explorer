#include "board.hpp"
#include "credentials.hpp"
#include "fred.hpp"
#include "http_client.hpp"
#include "key_setup.hpp"
#include "remote_store.hpp"
#include "secret_store.hpp"
#include "sqlite_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace datawire;

namespace {

std::string mask(const std::string& k) {
  if (k.size() <= 4) return "****";
  return std::string(k.size() - 4, '*') + k.substr(k.size() - 4);
}

// Reconcile the local SQLite store with the datawire-server: push every local
// series up, pull server-only series down (local wins — never clobbered). The
// terminal keeps reading from SQLite; this is the explicit online sync step.
int runSync() {
  auto envOr = [](const char* k, const char* d) {
    const char* v = std::getenv(k);
    return std::string(v && *v ? v : d);
  };
  const std::string url = envOr("DATAWIRE_SERVER_URL", "https://127.0.0.1:8080");
  const std::string ca = envOr("DATAWIRE_SERVER_CA", "");

  SqliteStore local(SqliteStore::defaultDbPath());
  RemoteStore remote(url, ca);

  std::vector<std::string> remoteIds;
  try {
    remoteIds = remote.listIds();  // also our connectivity check
  } catch (const std::exception& e) {
    std::cerr << "sync: server unreachable at " << url << " (" << e.what() << ")\n";
    if (ca.empty())
      std::cerr << "  self-signed dev cert? point DATAWIRE_SERVER_CA at the server's cert.\n";
    return 1;
  }

  int pushed = 0, pushErr = 0;
  for (const auto& id : local.listIds()) {
    const auto s = local.get(id);
    if (!s) continue;
    try {
      remote.put(id, s->series);
      ++pushed;
    } catch (const std::exception& e) {
      ++pushErr;
      std::cerr << "  push " << id << " failed: " << e.what() << "\n";
    }
  }

  int pulled = 0;
  for (const auto& id : remoteIds) {
    if (local.get(id)) continue;  // keep local copy; don't overwrite
    if (const auto s = remote.get(id)) {
      local.put(id, s->series);
      ++pulled;
    }
  }

  std::cout << "sync (" << url << "): pushed " << pushed << ", pulled " << pulled;
  if (pushErr) std::cout << ", " << pushErr << " push errors";
  std::cout << "\n";
  return pushErr ? 1 : 0;
}

int runKeyCommand(int argc, char** argv) {
  const std::string sub = argc > 2 ? argv[2] : "status";
  if (sub == "set") {
    const int rc = runKeySetup(*defaultSecretStore());  // masked entry + live FRED validation
    std::cout << (rc == 0 ? "Key validated and saved.\n" : "Key not saved.\n");
    return rc;
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

  if (cmd == "sync") return runSync();

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
    std::cerr << "usage: datawire [board [watchlist]] | get <ID> | search <text> | sync | key [set]\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
