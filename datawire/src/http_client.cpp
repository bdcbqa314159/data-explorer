#include "http_client.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace datawire {

CurlGlobal::CurlGlobal() {
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    throw std::runtime_error("curl_global_init failed");
  }
}
CurlGlobal::~CurlGlobal() { curl_global_cleanup(); }

namespace {

size_t appendToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  const size_t bytes = size * nmemb;
  out->append(ptr, bytes);
  return bytes;
}

struct EasyHandle {
  CURL* h = curl_easy_init();
  EasyHandle() {
    if (!h) throw std::runtime_error("curl_easy_init failed");
  }
  ~EasyHandle() {
    if (h) curl_easy_cleanup(h);
  }
  EasyHandle(const EasyHandle&) = delete;
  EasyHandle& operator=(const EasyHandle&) = delete;
};

// mbedTLS has no OS trust store, so curl needs an explicit CA bundle to verify
// public HTTPS (e.g. FRED). Prefer $DATAWIRE_CA_BUNDLE, else the vendored one
// compiled in via DATAWIRE_CA_BUNDLE_PATH. Empty -> curl uses its own default.
std::string caBundle() {
  if (const char* env = std::getenv("DATAWIRE_CA_BUNDLE"); env && *env) return env;
#ifdef DATAWIRE_CA_BUNDLE_PATH
  return DATAWIRE_CA_BUNDLE_PATH;
#else
  return {};
#endif
}

}  // namespace

std::string httpGet(const std::string& url, long timeoutSeconds) {
  EasyHandle easy;
  std::string body;

  curl_easy_setopt(easy.h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy.h, CURLOPT_WRITEFUNCTION, appendToString);
  curl_easy_setopt(easy.h, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(easy.h, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy.h, CURLOPT_TIMEOUT, timeoutSeconds);
  curl_easy_setopt(easy.h, CURLOPT_USERAGENT, "datawire/0.1");
  curl_easy_setopt(easy.h, CURLOPT_ACCEPT_ENCODING, "");

  // mbedTLS has no OS trust store: give it our CA bundle and verify the peer.
  const std::string ca = caBundle();
  if (!ca.empty()) curl_easy_setopt(easy.h, CURLOPT_CAINFO, ca.c_str());
  curl_easy_setopt(easy.h, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(easy.h, CURLOPT_SSL_VERIFYHOST, 2L);

  const CURLcode rc = curl_easy_perform(easy.h);
  if (rc != CURLE_OK) {
    throw std::runtime_error("GET failed for " + url + ": " + curl_easy_strerror(rc));
  }
  long httpCode = 0;
  curl_easy_getinfo(easy.h, CURLINFO_RESPONSE_CODE, &httpCode);
  if (httpCode >= 400) {
    throw std::runtime_error("GET " + url + " returned HTTP " + std::to_string(httpCode));
  }
  return body;
}

}  // namespace datawire
