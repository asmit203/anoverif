cmake_minimum_required(VERSION 3.16)
project(anoverif)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMICROHTTPD REQUIRED libmicrohttpd)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

# Compiler optimizations for high performance
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Include directories
include_directories(${LIBMICROHTTPD_INCLUDE_DIRS})
include_directories(${JSONCPP_INCLUDE_DIRS})
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${CURL_INCLUDE_DIRS})

# Main anonymization server
add_executable(anon_server
    src/anon_server.cpp
    src/hash_utils.cpp
    src/http_client.cpp
    src/config.cpp
)

target_link_libraries(anon_server 
    ${LIBMICROHTTPD_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${CURL_LIBRARIES}
    Threads::Threads
)

target_compile_options(anon_server PRIVATE ${LIBMICROHTTPD_CFLAGS_OTHER})

# Dummy API server for testing
add_executable(dummy_api
    src/dummy_api.cpp
)

target_link_libraries(dummy_api 
    ${LIBMICROHTTPD_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    Threads::Threads
)

target_compile_options(dummy_api PRIVATE ${LIBMICROHTTPD_CFLAGS_OTHER})

# Test client
add_executable(test_client
    src/test_client.cpp
)

target_link_libraries(test_client 
    ${JSONCPP_LIBRARIES}
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    Threads::Threads
)

# Build the advanced benchmark
add_executable(advanced_benchmark src/advanced_benchmark.cpp)
target_link_libraries(advanced_benchmark ${CURL_LIBRARIES} ${JSONCPP_LIBRARIES})
target_include_directories(advanced_benchmark PRIVATE ${CURL_INCLUDE_DIRS} ${JSONCPP_INCLUDE_DIRS})

# Build the simple timing benchmark
add_executable(simple_timing_benchmark src/simple_timing_benchmark.cpp)
target_link_libraries(simple_timing_benchmark ${CURL_LIBRARIES} ${JSONCPP_LIBRARIES})
target_include_directories(simple_timing_benchmark PRIVATE ${CURL_INCLUDE_DIRS} ${JSONCPP_INCLUDE_DIRS})

# Create src directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src)
