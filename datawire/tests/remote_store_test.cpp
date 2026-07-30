// RemoteStore graceful-offline check — no server required. Pointing at a dead
// port must yield nullopt (not throw), so the terminal degrades cleanly offline.
#include "http_client.hpp"
#include "remote_store.hpp"

#include <cassert>
#include <cstdio>

using namespace datawire;

int main() {
  CurlGlobal curl;  // libcurl init
  RemoteStore rs("https://127.0.0.1:1", "");  // nothing is listening on port 1
  assert(!rs.get("ANYTHING").has_value());     // connection refused -> nullopt, no throw
  std::puts("remote_store_test OK");
  return 0;
}
