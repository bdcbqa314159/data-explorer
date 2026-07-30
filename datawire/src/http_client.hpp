#pragma once
#include <string>

namespace datawire {

// One-time libcurl global init/cleanup, RAII. Construct once in main before any
// httpGet call (curl's global init is not thread-safe).
class CurlGlobal {
public:
  CurlGlobal();
  ~CurlGlobal();
  CurlGlobal(const CurlGlobal&) = delete;
  CurlGlobal& operator=(const CurlGlobal&) = delete;
};

// GET a URL, return the body. Throws std::runtime_error on failure (net/TLS/HTTP>=400).
// caPath, if set, is a CA cert file to verify the server against (e.g. the dev cert).
std::string httpGet(const std::string& url, long timeoutSeconds = 20, const std::string& caPath = "");

// POST a body, return the response body. Throws on net/TLS/HTTP>=400.
std::string httpPost(const std::string& url, const std::string& body,
                     const std::string& contentType = "application/json",
                     long timeoutSeconds = 20, const std::string& caPath = "");

}  // namespace datawire
