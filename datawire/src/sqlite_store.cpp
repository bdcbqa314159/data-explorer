#include "sqlite_store.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace datawire {
namespace {

long long nowSec() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void exec(sqlite3* db, const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string msg = err ? err : "exec failed";
    sqlite3_free(err);
    throw std::runtime_error("SqliteStore: " + msg);
  }
}

sqlite3_stmt* prepare(sqlite3* db, const char* sql) {
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
    throw std::runtime_error(std::string("SqliteStore: ") + sqlite3_errmsg(db));
  return st;
}

void bindText(sqlite3_stmt* st, int i, const std::string& v) {
  sqlite3_bind_text(st, i, v.c_str(), -1, SQLITE_TRANSIENT);
}

std::string colText(sqlite3_stmt* st, int i) {
  const unsigned char* t = sqlite3_column_text(st, i);
  return t ? reinterpret_cast<const char*>(t) : "";
}

}  // namespace

std::string SqliteStore::defaultDbPath() {
#if defined(_WIN32)
  if (const char* p = std::getenv("LOCALAPPDATA")) return std::string(p) + "\\datawire\\datawire.db";
  if (const char* u = std::getenv("USERPROFILE")) return std::string(u) + "\\datawire\\datawire.db";
#else
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/share/datawire/datawire.db";
#endif
  return "datawire.db";
}

SqliteStore::SqliteStore(std::string dbPath) {
  if (dbPath != ":memory:") {
    std::error_code ec;
    fs::create_directories(fs::path(dbPath).parent_path(), ec);  // best effort
  }
  if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
    std::string msg = db_ ? sqlite3_errmsg(db_) : "open failed";
    sqlite3_close(db_);
    throw std::runtime_error("SqliteStore: " + msg);
  }
  exec(db_, "PRAGMA journal_mode=WAL;");
  exec(db_,
       "CREATE TABLE IF NOT EXISTS series("
       " id TEXT PRIMARY KEY, title TEXT, unit TEXT, frequency TEXT,"
       " seasonal_adj TEXT, as_of TEXT, source_url TEXT, source TEXT,"
       " fetched_at INTEGER);"
       "CREATE TABLE IF NOT EXISTS observation("
       " series_id TEXT, date TEXT, value REAL,"
       " PRIMARY KEY(series_id, date));");
}

SqliteStore::~SqliteStore() { sqlite3_close(db_); }

std::optional<StoredSeries> SqliteStore::get(const std::string& id) {
  sqlite3_stmt* st = prepare(
      db_,
      "SELECT title,unit,frequency,seasonal_adj,as_of,source_url,source,fetched_at"
      " FROM series WHERE id=?1;");
  bindText(st, 1, id);
  if (sqlite3_step(st) != SQLITE_ROW) {
    sqlite3_finalize(st);
    return std::nullopt;
  }

  Series s;
  s.origin = Origin::LocalDb;  // served from the local SQLite store
  s.meta.id = id;
  s.meta.title = colText(st, 0);
  s.meta.unit = colText(st, 1);
  s.meta.frequency = colText(st, 2);
  s.meta.seasonalAdj = colText(st, 3);
  s.meta.asOf = colText(st, 4);
  s.meta.sourceUrl = colText(st, 5);
  s.meta.source = colText(st, 6);
  const long long fetchedAt = sqlite3_column_int64(st, 7);
  sqlite3_finalize(st);

  st = prepare(db_, "SELECT date,value FROM observation WHERE series_id=?1 ORDER BY date;");
  bindText(st, 1, id);
  while (sqlite3_step(st) == SQLITE_ROW)
    s.observations.push_back({colText(st, 0), sqlite3_column_double(st, 1)});
  sqlite3_finalize(st);

  return StoredSeries{std::move(s), nowSec() - fetchedAt};
}

std::vector<std::string> SqliteStore::listIds() {
  std::vector<std::string> out;
  sqlite3_stmt* st = prepare(db_, "SELECT id FROM series ORDER BY id;");
  while (sqlite3_step(st) == SQLITE_ROW) out.push_back(colText(st, 0));
  sqlite3_finalize(st);
  return out;
}

void SqliteStore::put(const std::string& id, const Series& s) {
  exec(db_, "BEGIN;");
  try {
    sqlite3_stmt* st = prepare(
        db_,
        "INSERT INTO series(id,title,unit,frequency,seasonal_adj,as_of,source_url,source,fetched_at)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)"
        " ON CONFLICT(id) DO UPDATE SET title=?2,unit=?3,frequency=?4,seasonal_adj=?5,"
        " as_of=?6,source_url=?7,source=?8,fetched_at=?9;");
    bindText(st, 1, id);
    bindText(st, 2, s.meta.title);
    bindText(st, 3, s.meta.unit);
    bindText(st, 4, s.meta.frequency);
    bindText(st, 5, s.meta.seasonalAdj);
    bindText(st, 6, s.meta.asOf);
    bindText(st, 7, s.meta.sourceUrl);
    bindText(st, 8, s.meta.source);
    sqlite3_bind_int64(st, 9, nowSec());
    sqlite3_step(st);
    sqlite3_finalize(st);

    // Observations are replaced wholesale — a fetch is the authoritative set.
    st = prepare(db_, "DELETE FROM observation WHERE series_id=?1;");
    bindText(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    st = prepare(db_, "INSERT INTO observation(series_id,date,value) VALUES(?1,?2,?3);");
    for (const auto& o : s.observations) {
      bindText(st, 1, id);
      bindText(st, 2, o.date);
      sqlite3_bind_double(st, 3, o.value);
      sqlite3_step(st);
      sqlite3_reset(st);
    }
    sqlite3_finalize(st);

    exec(db_, "COMMIT;");
  } catch (...) {
    exec(db_, "ROLLBACK;");
    throw;
  }
}

}  // namespace datawire
