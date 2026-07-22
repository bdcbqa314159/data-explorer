cmake_minimum_required(VERSION 3.21)
project(feedwire LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies come from vcpkg (built from source). See README.md.
find_package(CURL CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

# --- main binary ---------------------------------------------------------
add_executable(feedwire
  src/main.cpp
  src/http_client.cpp
  src/feed.cpp
  src/cache.cpp
  src/read_store.cpp
)
target_include_directories(feedwire PRIVATE src)
target_link_libraries(feedwire PRIVATE CURL::libcurl pugixml::pugixml)

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
