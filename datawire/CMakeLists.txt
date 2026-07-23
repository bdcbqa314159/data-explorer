cmake_minimum_required(VERSION 3.21)
project(datawire LANGUAGES CXX)

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

FetchContent_MakeAvailable(curl nlohmann_json)

# --- main binary ---------------------------------------------------------
add_executable(datawire
  src/main.cpp
  src/http_client.cpp
  src/fred_parse.cpp
  src/fred_fetch.cpp
)
target_include_directories(datawire PRIVATE src)
target_link_libraries(datawire PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

# --- tests ---------------------------------------------------------------
# Pure parser tests — no network, no curl.
enable_testing()
add_executable(fred_test tests/fred_test.cpp src/fred_parse.cpp)
target_include_directories(fred_test PRIVATE src)
target_link_libraries(fred_test PRIVATE nlohmann_json::nlohmann_json)
add_test(NAME fred_test COMMAND fred_test)
