#include "board.hpp"
#include "credentials.hpp"
#include "fred.hpp"
#include "http_client.hpp"
#include "key_setup.hpp"
#include "remote_store.hpp"
#include "secret_store.hpp"
#include "sqlite_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
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

std::string humanAge(long long sec) {
  if (sec < 0) sec = 0;
  if (sec < 90) return std::to_string(sec) + "s";
  if (sec < 90 * 60) return std::to_string(sec / 60) + "m";
  if (sec < 48 * 3600) return std::to_string(sec / 3600) + "h";
  return std::to_string(sec / 86400) + "d";
}

// `datawire list` — catalog of everything in the local SQLite store.
int runList() {
  SqliteStore local(SqliteStore::defaultDbPath());
  const auto ids = local.listIds();
  if (ids.empty()) {
    std::cout << "(store empty — run the board or `datawire get <ID>` to fetch some series)\n";
    return 0;
  }
  std::printf("%-14s %-9s %7s  %-11s %6s  %s\n", "ID", "FREQ", "POINTS", "LAST", "AGE", "TITLE");
  for (const auto& id : ids) {
    const auto s = local.get(id);
    if (!s) continue;
    const auto& m = s->series.meta;
    const auto& obs = s->series.observations;
    std::string freq = m.frequency;  // "Quarterly, End of Period" -> "Quarterly"
    if (const auto cut = freq.find_first_of(", "); cut != std::string::npos) freq = freq.substr(0, cut);
    const std::string title = m.title.size() > 44 ? m.title.substr(0, 43) + "…" : m.title;
    std::printf("%-14s %-9s %7zu  %-11s %6s  %s\n", id.c_str(), freq.c_str(), obs.size(),
                obs.empty() ? "-" : obs.back().date.c_str(), humanAge(s->ageSec).c_str(),
                title.c_str());
  }
  return 0;
}

// `datawire export <ID> [--json]` — dump a stored series to stdout (CSV default).
int runExport(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: datawire export <ID> [--json]\n";
    return 2;
  }
  const std::string id = argv[2];
  const bool json = argc > 3 && std::string(argv[3]) == "--json";
  SqliteStore local(SqliteStore::defaultDbPath());
  const auto s = local.get(id);
  if (!s) {
    std::cerr << "not in store: " << id << " (fetch it in the board or `datawire get " << id
              << "` first)\n";
    return 1;
  }
  const Series& series = s->series;
  if (json) {
    nlohmann::json meta = {{"id", series.meta.id},
                           {"title", series.meta.title},
                           {"unit", series.meta.unit},
                           {"frequency", series.meta.frequency},
                           {"seasonalAdj", series.meta.seasonalAdj},
                           {"asOf", series.meta.asOf},
                           {"sourceUrl", series.meta.sourceUrl},
                           {"source", series.meta.source}};
    auto obs = nlohmann::json::array();
    for (const auto& o : series.observations) obs.push_back({{"date", o.date}, {"value", o.value}});
    std::cout << nlohmann::json{{"meta", meta}, {"observations", obs}}.dump(2) << "\n";
  } else {
    std::cout << "date,value\n";
    for (const auto& o : series.observations) std::printf("%s,%.10g\n", o.date.c_str(), o.value);
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
//   datawire list                -> catalog of what's in the local store
//   datawire export UNRATE       -> dump a stored series to stdout (CSV; --json for JSON)
int main(int argc, char** argv) {
  const std::string cmd = argc > 1 ? argv[1] : "";

  if (cmd == "key") return runKeyCommand(argc, argv);
  if (cmd == "list") return runList();               // local store — no key needed
  if (cmd == "export") return runExport(argc, argv);  // local store — no key needed

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
    std::cerr << "usage: datawire [board [watchlist]] | list | export <ID> [--json] | "
                 "get <ID> | search <text> | sync | key [set]\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
