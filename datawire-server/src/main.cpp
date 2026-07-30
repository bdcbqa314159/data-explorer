// datawire-server — M6 slice 1: connect to MySQL via Boost.MySQL, apply the
// schema, and round-trip a probe series. Proves the DB layer end to end before
// the store (slice 2) and the HTTP endpoints (slice 3) go on top.
//
// Connection is env-configurable (localhost/root/no-password defaults match a
// fresh `brew install mysql`). TLS is disabled for localhost dev; it lands in
// M7 when the terminal talks to the server over the wire.

#include <boost/asio/io_context.hpp>
#include <boost/mysql.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace mysql = boost::mysql;

namespace {

std::string env(const char* key, const char* fallback) {
  const char* v = std::getenv(key);
  return (v && *v) ? std::string(v) : std::string(fallback);
}

}  // namespace

int main() {
  boost::asio::io_context ctx;
  mysql::any_connection conn(ctx);

  mysql::connect_params params;
  params.server_address.emplace_host_and_port(env("DATAWIRE_DB_HOST", "127.0.0.1"), 3306);
  params.username = env("DATAWIRE_DB_USER", "root");
  params.password = env("DATAWIRE_DB_PASS", "");
  params.ssl = mysql::ssl_mode::disable;  // localhost dev; TLS in M7
  params.multi_queries = false;

  try {
    conn.connect(params);
    mysql::results r;

    conn.execute("CREATE DATABASE IF NOT EXISTS datawire", r);
    conn.execute("USE datawire", r);
    conn.execute(
        "CREATE TABLE IF NOT EXISTS series("
        " id VARCHAR(64) PRIMARY KEY, title TEXT, unit VARCHAR(64), frequency VARCHAR(64),"
        " seasonal_adj VARCHAR(64), as_of VARCHAR(32), source_url TEXT, source VARCHAR(32),"
        " fetched_at BIGINT)",
        r);
    conn.execute(
        "CREATE TABLE IF NOT EXISTS observation("
        " series_id VARCHAR(64) NOT NULL, obs_date VARCHAR(16) NOT NULL, value DOUBLE,"
        " PRIMARY KEY(series_id, obs_date))",
        r);

    // Upsert a probe row, then read it back — the round-trip.
    mysql::statement up = conn.prepare_statement(
        "INSERT INTO series(id,title,unit,frequency,seasonal_adj,as_of,source_url,source,fetched_at)"
        " VALUES(?,?,?,?,?,?,?,?,?)"
        " ON DUPLICATE KEY UPDATE title=VALUES(title), fetched_at=VALUES(fetched_at)");
    conn.execute(up.bind("PROBE", "Probe Series", "idx", "Monthly", "SA", "2026-07-01",
                         "https://example.org/PROBE", "TEST", std::int64_t{123}),
                 r);

    conn.execute("SELECT id, title, fetched_at FROM series WHERE id = 'PROBE'", r);
    conn.close();

    if (r.rows().empty()) {
      std::cerr << "datawire-server: round-trip FAILED (no row read back)\n";
      return 1;
    }
    const auto row = r.rows().at(0);
    std::cout << "datawire-server: DB round-trip OK — "
              << row.at(0).as_string() << " / " << row.at(1).as_string()
              << " @ " << row.at(2).as_int64() << "\n";
    return 0;
  } catch (const mysql::error_with_diagnostics& e) {
    std::cerr << "MySQL error: " << e.what() << "\n"
              << "server: " << e.get_diagnostics().server_message() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n"
              << "(is MySQL running? `brew services start mysql`)\n";
    return 1;
  }
}
