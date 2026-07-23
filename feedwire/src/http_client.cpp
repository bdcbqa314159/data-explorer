#include "http_client.hpp"

#include <curl/curl.h>

#include <stdexcept>
#include <string>

namespace feedwire {

CurlGlobal::CurlGlobal() {
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    throw std::runtime_error("curl_global_init failed");
  }
}
CurlGlobal::~CurlGlobal() { curl_global_cleanup(); }

namespace {

// libcurl write callback: append received bytes to the std::string we passed in.
size_t appendToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  const size_t bytes = size * nmemb;
  out->append(ptr, bytes);
  return bytes;
}

// curl progress callback: return non-zero to abort the transfer. We use it only
// to poll a caller-supplied cancel flag.
int abortIfCancelled(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  const auto* cancel = static_cast<const std::atomic<bool>*>(userdata);
  return (cancel && cancel->load()) ? 1 : 0;
}

// RAII for a curl easy handle so we never leak it, even if setopt/perform throw.
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

}  // namespace

std::string httpGet(const std::string& url, long timeoutSeconds,
                    const std::atomic<bool>* cancel) {
  EasyHandle easy;
  std::string body;

  curl_easy_setopt(easy.h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy.h, CURLOPT_WRITEFUNCTION, appendToString);
  curl_easy_setopt(easy.h, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(easy.h, CURLOPT_FOLLOWLOCATION, 1L);      // follow redirects
  curl_easy_setopt(easy.h, CURLOPT_TIMEOUT, timeoutSeconds);
  curl_easy_setopt(easy.h, CURLOPT_USERAGENT, "feedwire/0.1");
  curl_easy_setopt(easy.h, CURLOPT_ACCEPT_ENCODING, "");     // enable gzip/deflate
  if (cancel) {
    curl_easy_setopt(easy.h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy.h, CURLOPT_XFERINFOFUNCTION, abortIfCancelled);
    curl_easy_setopt(easy.h, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool>*>(cancel));
  }

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

}  // namespace feedwire
