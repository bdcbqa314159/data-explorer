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
find_package(Boost 1.86 REQUIRED COMPONENTS charconv)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(datawire-server src/main.cpp)
target_link_libraries(datawire-server PRIVATE
  Boost::headers
  Boost::charconv
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
)
