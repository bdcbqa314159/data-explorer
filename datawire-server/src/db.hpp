#pragma once
#include "series.hpp"  // shared model from ../datawire/src

#include <boost/asio/io_context.hpp>
#include <boost/mysql/any_connection.hpp>

#include <optional>
#include <string>
#include <vector>

namespace datawire::server {

// The canonical server-side store, backed by MySQL via Boost.MySQL. Connects
// and applies the schema on construction (env-configured; defaults match a
// fresh `brew install mysql`). Same semantics as the terminal's SqliteStore:
// a series is upserted with its full observation set, read back whole.
class Db {
public:
  Db();  // connects + ensures schema; throws boost::mysql::error_with_diagnostics on failure

  void upsertSeries(const Series& s);                 // meta + full observation set (transactional)
  std::optional<Series> getSeries(const std::string& id);
  std::vector<std::string> listSeriesIds();

private:
  boost::asio::io_context ctx_;
  boost::mysql::any_connection conn_;
};

}  // namespace datawire::server
