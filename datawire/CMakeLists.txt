cmake_minimum_required(VERSION 3.21)
project(datawire LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Deps downloaded from GitHub and built from source via FetchContent (like feedwire).
include(FetchContent)

# --- libcurl: HTTP + TLS (native backend per platform) -------------------
set(BUILD_TESTING OFF)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
if(WIN32)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
elseif(APPLE)
  set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
else()
  set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)  # needs system OpenSSL dev headers
endif()
FetchContent_Declare(curl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG        curl-8_11_0
  GIT_SHALLOW    TRUE
)

# --- nlohmann/json: FRED/Eurostat JSON parsing ---------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)

# --- FTXUI: terminal UI for the two-pane board ---------------------------
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(curl nlohmann_json ftxui)

# --- sqlite3: vendored amalgamation (offline, no fetch) ------------------
# Committed under third_party/sqlite so the build needs no network. Backs the
# local SqliteStore — the terminal's offline source of truth.
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_DQS=0)
if(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

# --- datawire_core: the SDK (data layer, no UI) --------------------------
# Everything a non-TUI client would need: model, source (FRED), store (cache),
# analysis. The TUI links this; a future CLI or another frontend could too.
add_library(datawire_core STATIC
  src/credentials.cpp
  src/http_client.cpp
  src/fred_parse.cpp
  src/fred_fetch.cpp
  src/fred_source.cpp
  src/json_store.cpp
  src/sqlite_store.cpp
  src/datawire.cpp
  src/window.cpp
  src/cache.cpp
)
target_include_directories(datawire_core PUBLIC src)
target_link_libraries(datawire_core PUBLIC
  CURL::libcurl
  nlohmann_json::nlohmann_json
)
target_link_libraries(datawire_core PRIVATE sqlite3)

# --- datawire: the terminal UI (thin client of datawire_core) ------------
add_executable(datawire
  src/main.cpp
  src/board.cpp
)
target_link_libraries(datawire PRIVATE
  datawire_core
  ftxui::component ftxui::dom ftxui::screen
)

# --- tests ---------------------------------------------------------------
# Pure parser tests — no network, no curl.
enable_testing()
add_executable(fred_test tests/fred_test.cpp src/fred_parse.cpp)
target_include_directories(fred_test PRIVATE src)
target_link_libraries(fred_test PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME fred_test COMMAND fred_test)

# window math — pure, no deps.
add_executable(window_test tests/window_test.cpp src/window.cpp)
target_include_directories(window_test PRIVATE src)
add_test(NAME window_test COMMAND window_test)

# SqliteStore round-trip — in-memory DB, no network.
add_executable(sqlite_store_test tests/sqlite_store_test.cpp src/sqlite_store.cpp)
target_include_directories(sqlite_store_test PRIVATE src)
target_link_libraries(sqlite_store_test PRIVATE sqlite3)
add_test(NAME sqlite_store_test COMMAND sqlite_store_test)
