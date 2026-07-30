#include "db.hpp"

#include <boost/mysql.hpp>

#include <cstdint>
#include <cstdlib>
#include <ctime>

namespace mysql = boost::mysql;

namespace datawire::server {
namespace {

std::string env(const char* key, const char* fallback) {
  const char* v = std::getenv(key);
  return (v && *v) ? std::string(v) : std::string(fallback);
}

long long nowSec() { return static_cast<long long>(std::time(nullptr)); }

std::string str(const mysql::field_view& f) {
  return f.is_null() ? std::string{} : std::string(f.as_string());
}

}  // namespace

Db::Db() : conn_(ctx_) {
  mysql::connect_params p;
  p.server_address.emplace_host_and_port(env("DATAWIRE_DB_HOST", "127.0.0.1"), 3306);
  p.username = env("DATAWIRE_DB_USER", "root");
  p.password = env("DATAWIRE_DB_PASS", "");
  p.ssl = mysql::ssl_mode::disable;  // localhost dev; TLS in M7
  conn_.connect(p);

  mysql::results r;
  conn_.execute("CREATE DATABASE IF NOT EXISTS datawire", r);
  conn_.execute("USE datawire", r);
  conn_.execute(
      "CREATE TABLE IF NOT EXISTS series("
      " id VARCHAR(64) PRIMARY KEY, title TEXT, unit VARCHAR(64), frequency VARCHAR(64),"
      " seasonal_adj VARCHAR(64), as_of VARCHAR(32), source_url TEXT, source VARCHAR(32),"
      " fetched_at BIGINT)",
      r);
  conn_.execute(
      "CREATE TABLE IF NOT EXISTS observation("
      " series_id VARCHAR(64) NOT NULL, obs_date VARCHAR(16) NOT NULL, value DOUBLE,"
      " PRIMARY KEY(series_id, obs_date))",
      r);
}

void Db::upsertSeries(const Series& s) {
  const std::string& id = s.meta.id;
  mysql::results r;
  conn_.execute("START TRANSACTION", r);
  try {
    mysql::statement up = conn_.prepare_statement(
        "INSERT INTO series(id,title,unit,frequency,seasonal_adj,as_of,source_url,source,fetched_at)"
        " VALUES(?,?,?,?,?,?,?,?,?)"
        " ON DUPLICATE KEY UPDATE title=VALUES(title),unit=VALUES(unit),frequency=VALUES(frequency),"
        " seasonal_adj=VALUES(seasonal_adj),as_of=VALUES(as_of),source_url=VALUES(source_url),"
        " source=VALUES(source),fetched_at=VALUES(fetched_at)");
    conn_.execute(up.bind(id, s.meta.title, s.meta.unit, s.meta.frequency, s.meta.seasonalAdj,
                          s.meta.asOf, s.meta.sourceUrl, s.meta.source, std::int64_t{nowSec()}),
                  r);

    // Observations replaced wholesale — a fetch is the authoritative set.
    mysql::statement del = conn_.prepare_statement("DELETE FROM observation WHERE series_id=?");
    conn_.execute(del.bind(id), r);
    mysql::statement ins =
        conn_.prepare_statement("INSERT INTO observation(series_id,obs_date,value) VALUES(?,?,?)");
    for (const auto& o : s.observations) conn_.execute(ins.bind(id, o.date, o.value), r);

    conn_.execute("COMMIT", r);
  } catch (...) {
    mysql::results rr;
    conn_.execute("ROLLBACK", rr);
    throw;
  }
}

std::optional<Series> Db::getSeries(const std::string& id) {
  mysql::results r;
  mysql::statement q = conn_.prepare_statement(
      "SELECT title,unit,frequency,seasonal_adj,as_of,source_url,source FROM series WHERE id=?");
  conn_.execute(q.bind(id), r);
  if (r.rows().empty()) return std::nullopt;

  Series s;
  s.meta.id = id;
  const auto row = r.rows().at(0);
  s.meta.title = str(row.at(0));
  s.meta.unit = str(row.at(1));
  s.meta.frequency = str(row.at(2));
  s.meta.seasonalAdj = str(row.at(3));
  s.meta.asOf = str(row.at(4));
  s.meta.sourceUrl = str(row.at(5));
  s.meta.source = str(row.at(6));

  mysql::statement q2 = conn_.prepare_statement(
      "SELECT obs_date,value FROM observation WHERE series_id=? ORDER BY obs_date");
  conn_.execute(q2.bind(id), r);
  for (const auto o : r.rows())
    s.observations.push_back({std::string(o.at(0).as_string()), o.at(1).as_double()});
  return s;
}

std::vector<std::string> Db::listSeriesIds() {
  mysql::results r;
  conn_.execute("SELECT id FROM series ORDER BY id", r);
  std::vector<std::string> out;
  for (const auto o : r.rows()) out.push_back(std::string(o.at(0).as_string()));
  return out;
}

}  // namespace datawire::server
