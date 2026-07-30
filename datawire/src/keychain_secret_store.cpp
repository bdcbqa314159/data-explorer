#include "keychain_secret_store.hpp"

#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace datawire {
namespace {

constexpr const char* kService = "datawire";

CFStringRef cfstr(const std::string& s) {
  return CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(s.data()),
                                 static_cast<CFIndex>(s.size()), kCFStringEncodingUTF8, false);
}

// Query dict matching one generic-password item (service, account). Caller CFReleases.
CFMutableDictionaryRef baseQuery(const std::string& account) {
  CFMutableDictionaryRef q = CFDictionaryCreateMutable(
      nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
  CFStringRef svc = cfstr(kService);
  CFDictionarySetValue(q, kSecAttrService, svc);
  CFRelease(svc);
  CFStringRef acc = cfstr(account);
  CFDictionarySetValue(q, kSecAttrAccount, acc);
  CFRelease(acc);
  return q;
}

}  // namespace

std::optional<std::string> KeychainSecretStore::get(const std::string& name) {
  CFMutableDictionaryRef q = baseQuery(name);
  CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
  CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
  CFTypeRef result = nullptr;
  const OSStatus st = SecItemCopyMatching(q, &result);
  CFRelease(q);
  if (st != errSecSuccess || result == nullptr) return std::nullopt;
  CFDataRef data = reinterpret_cast<CFDataRef>(result);
  std::string out(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                  static_cast<size_t>(CFDataGetLength(data)));
  CFRelease(result);
  return out;
}

void KeychainSecretStore::set(const std::string& name, const std::string& value) {
  remove(name);  // simplest upsert: delete any existing item, then add
  CFMutableDictionaryRef q = baseQuery(name);
  CFDataRef data =
      CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(value.data()),
                   static_cast<CFIndex>(value.size()));
  CFDictionarySetValue(q, kSecValueData, data);
  SecItemAdd(q, nullptr);
  CFRelease(data);
  CFRelease(q);
}

void KeychainSecretStore::remove(const std::string& name) {
  CFMutableDictionaryRef q = baseQuery(name);
  SecItemDelete(q);  // errSecItemNotFound is fine
  CFRelease(q);
}

}  // namespace datawire

#endif  // __APPLE__
