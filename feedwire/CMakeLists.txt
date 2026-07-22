cmake_minimum_required(VERSION 3.21)
project(feedwire LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies are downloaded from GitHub and built from source via FetchContent.
# No package manager needed. See README.md.
include(FetchContent)

# --- pugixml: tiny XML parser, trivial to build --------------------------
FetchContent_Declare(pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG        v1.14
  GIT_SHALLOW    TRUE
)

# --- libcurl: HTTP + TLS -------------------------------------------------
# Use the OS-native TLS backend so we don't also compile OpenSSL from source:
#   Windows -> Schannel, macOS -> Secure Transport, Linux -> system OpenSSL.
# Slimmed to HTTP(S) only, static, no exe/tests/zlib.
set(BUILD_TESTING OFF)                            # skip curl's own test suite
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)   # skip man-page generation
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
if(WIN32)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
elseif(APPLE)
  set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
else()
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)    # needs system OpenSSL dev headers
endif()
FetchContent_Declare(curl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG        curl-8_11_0
  GIT_SHALLOW    TRUE
)

# --- FTXUI: terminal UI (Phase 2) ----------------------------------------
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(pugixml curl ftxui)

# --- main binary ---------------------------------------------------------
add_executable(feedwire
  src/main.cpp
  src/http_client.cpp
  src/feed.cpp
  src/cache.cpp
  src/read_store.cpp
  src/tui.cpp
)
target_include_directories(feedwire PRIVATE src)
target_link_libraries(feedwire PRIVATE
  CURL::libcurl
  pugixml::pugixml
  ftxui::component ftxui::dom ftxui::screen
)

# --- tests ---------------------------------------------------------------
# No framework — plain asserts, run via CTest. Cross-platform, zero extra deps.
enable_testing()

add_executable(feed_test tests/feed_test.cpp src/feed.cpp)
target_include_directories(feed_test PRIVATE src)
target_link_libraries(feed_test PRIVATE pugixml::pugixml)
add_test(NAME feed_test COMMAND feed_test)

# store_test needs no third-party deps (filesystem only).
add_executable(store_test tests/store_test.cpp src/cache.cpp src/read_store.cpp)
target_include_directories(store_test PRIVATE src)
add_test(NAME store_test COMMAND store_test)
