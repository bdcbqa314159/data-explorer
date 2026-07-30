cmake_minimum_required(VERSION 3.21)
project(datawire_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Server tier deps come from the system (brew), NOT FetchContent — this binary
# is dev-time infra (it needs a live MySQL + networking), unlike the offline
# terminal. Boost.MySQL is header-only but pulls in compiled Boost.Charconv,
# and Boost.Asio's TLS needs OpenSSL.
# Boost.MySQL / Asio / System are header-only (via Boost::headers); only
# Boost.Charconv is a compiled component it needs. Asio wants a threads lib.
find_package(Boost 1.86 REQUIRED COMPONENTS charconv json)  # Beast/Asio header-only via Boost::headers
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# The server: HTTP (Beast) + JSON over the MySQL-backed store.
add_executable(datawire-server src/main.cpp src/db.cpp)
target_include_directories(datawire-server PRIVATE ../datawire/src)  # shared series.hpp model
target_link_libraries(datawire-server PRIVATE
  Boost::headers Boost::charconv Boost::json
  OpenSSL::SSL OpenSSL::Crypto Threads::Threads
)

# Store self-test (needs a running MySQL; run manually, not part of ctest).
add_executable(datawire-server-selftest tests/selftest.cpp src/db.cpp)
target_include_directories(datawire-server-selftest PRIVATE src ../datawire/src)
target_link_libraries(datawire-server-selftest PRIVATE
  Boost::headers Boost::charconv
  OpenSSL::SSL OpenSSL::Crypto Threads::Threads
)

# End-to-end test: DB round-trip + live HTTP endpoint assertions.
# Needs MySQL; the script exits 77 (ctest SKIP) when it's unreachable.
enable_testing()
add_test(NAME server_e2e COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh)
set_tests_properties(server_e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
