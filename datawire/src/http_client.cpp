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

void applyCommon(CURL* h, const std::string& url, std::string* body, long timeout,
                 const std::string& caPath) {
  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, appendToString);
  curl_easy_setopt(h, CURLOPT_WRITEDATA, body);
  curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(h, CURLOPT_TIMEOUT, timeout);
  curl_easy_setopt(h, CURLOPT_USERAGENT, "datawire/0.1");
  curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
  // caPath (a specific cert, e.g. the dev server) overrides the public CA bundle.
  const std::string ca = caPath.empty() ? caBundle() : caPath;
  if (!ca.empty()) curl_easy_setopt(h, CURLOPT_CAINFO, ca.c_str());
  curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
}

// Perform and validate; throws on transport error or HTTP >= 400.
void check(CURL* h, const char* verb, const std::string& url) {
  const CURLcode rc = curl_easy_perform(h);
  if (rc != CURLE_OK)
    throw std::runtime_error(std::string(verb) + " failed for " + url + ": " + curl_easy_strerror(rc));
  long httpCode = 0;
  curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &httpCode);
  if (httpCode >= 400)
    throw std::runtime_error(std::string(verb) + " " + url + " returned HTTP " + std::to_string(httpCode));
}

}  // namespace

std::string httpGet(const std::string& url, long timeoutSeconds, const std::string& caPath) {
  EasyHandle easy;
  std::string body;
  applyCommon(easy.h, url, &body, timeoutSeconds, caPath);
  check(easy.h, "GET", url);
  return body;
}

std::string httpPost(const std::string& url, const std::string& reqBody,
                     const std::string& contentType, long timeoutSeconds,
                     const std::string& caPath) {
  EasyHandle easy;
  std::string body;
  applyCommon(easy.h, url, &body, timeoutSeconds, caPath);
  curl_easy_setopt(easy.h, CURLOPT_POST, 1L);
  curl_easy_setopt(easy.h, CURLOPT_POSTFIELDS, reqBody.c_str());
  curl_easy_setopt(easy.h, CURLOPT_POSTFIELDSIZE, static_cast<long>(reqBody.size()));
  curl_slist* headers = curl_slist_append(nullptr, ("Content-Type: " + contentType).c_str());
  curl_easy_setopt(easy.h, CURLOPT_HTTPHEADER, headers);
  try {
    check(easy.h, "POST", url);
  } catch (...) {
    curl_slist_free_all(headers);
    throw;
  }
  curl_slist_free_all(headers);
  return body;
}

}  // namespace datawire
