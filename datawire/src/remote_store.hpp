#pragma once
#include "store.hpp"

#include <string>
#include <vector>

namespace datawire {

// Store backed by datawire-server over HTTPS (behind the same Store interface as
// SqliteStore). get -> GET /series/:id, put -> POST /series. For the dev
// self-signed cert, pass caPath = the server cert; otherwise the vendored CA
// bundle verifies. This is the "online" half of the offline/online model — the
// terminal keeps reading locally from SQLite and syncs against this.
class RemoteStore : public Store {
public:
  explicit RemoteStore(std::string baseUrl, std::string caPath = "");

  std::optional<StoredSeries> get(const std::string& id) override;  // nullopt on 404/offline
  void put(const std::string& id, const Series& series) override;   // throws on failure
  std::vector<std::string> listIds();                               // GET /series

private:
  std::string baseUrl_;  // e.g. "https://127.0.0.1:8080" (no trailing slash)
  std::string caPath_;
};

}  // namespace datawire
