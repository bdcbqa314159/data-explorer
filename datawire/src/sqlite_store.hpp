#pragma once
#include "store.hpp"

#include <optional>
#include <string>

struct sqlite3;  // opaque; only sqlite_store.cpp includes <sqlite3.h>

namespace datawire {

// Store backed by a local SQLite file — the terminal's offline source of truth.
// Two tables: one row per series (meta + fetched_at), one row per observation.
// A future MysqlStore/RemoteStore drops in behind the same Store interface.
class SqliteStore : public Store {
public:
  // Default path: ~/.local/share/datawire/datawire.db (dirs created if absent).
  // Pass ":memory:" (or a temp path) in tests. Throws on open/schema failure.
  explicit SqliteStore(std::string dbPath = defaultDbPath());
  ~SqliteStore() override;

  SqliteStore(const SqliteStore&) = delete;
  SqliteStore& operator=(const SqliteStore&) = delete;

  std::optional<StoredSeries> get(const std::string& id) override;
  void put(const std::string& id, const Series& series) override;

  static std::string defaultDbPath();

private:
  sqlite3* db_ = nullptr;
};

}  // namespace datawire
