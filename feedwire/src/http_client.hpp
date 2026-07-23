#pragma once
#include <atomic>
#include <string>

namespace feedwire {

// One-time libcurl global init/cleanup, RAII. Construct exactly once, in main,
// before any httpGet call. libcurl's global init is NOT thread-safe, so it must
// happen before threads spin up.
class CurlGlobal {
public:
  CurlGlobal();
  ~CurlGlobal();
  CurlGlobal(const CurlGlobal&) = delete;
  CurlGlobal& operator=(const CurlGlobal&) = delete;
};

// GET a URL, return the body as text. Throws std::runtime_error on any failure
// (network, TLS, HTTP >= 400). Uses its own easy handle per call, so it is safe
// to call concurrently from std::async tasks.
//
// If `cancel` is non-null and becomes true mid-transfer, the request aborts
// promptly (throws) — lets a caller stop an in-flight fetch, e.g. on quit.
std::string httpGet(const std::string& url, long timeoutSeconds = 15,
                    const std::atomic<bool>* cancel = nullptr);

}  // namespace feedwire
