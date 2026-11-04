# Host unit test of how the HTTP server reads a request. It needs no
# network: the socket calls of httpserver.c are faked in the test.
#
#   cmake -S test/server -B build/server-test
#   cmake --build build/server-test
#   ctest --test-dir build/server-test --output-on-failure
#
# On Windows with MinGW add -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc.
# Only the qoraal headers are used; QORAAL_DIR points to the checkout, by
# default the one next to this repository.

cmake_minimum_required(VERSION 3.16)
project(httpserver_test C)

set(QORAAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../qoraal" CACHE PATH "qoraal checkout, for its headers")
if(NOT EXISTS "${QORAAL_DIR}/include/qoraal/qoraal.h")
    message(FATAL_ERROR "No qoraal headers in ${QORAAL_DIR}, set QORAAL_DIR")
endif()

set(HTTP_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")

enable_testing()

add_executable(httpserver_test test_httpserver.c ${HTTP_DIR}/src/httpparse.c)
target_include_directories(httpserver_test PRIVATE
    ${HTTP_DIR}/include
    ${QORAAL_DIR}/include)
# A small body limit keeps the test's bodies short.
target_compile_definitions(httpserver_test PRIVATE
    CFG_OS_POSIX
    CFG_HTTPSERVER_TLS_DISABLE=1
    CFG_HTTPCLIENT_TLS_DISABLE=1
    CFG_HTTPSERVER_CONTENT_MAX=64)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(test_httpserver.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
    target_compile_options(httpserver_test PRIVATE -g)
endif()
if(WIN32)
    target_link_libraries(httpserver_test ws2_32)
endif()
add_test(NAME httpserver_test COMMAND httpserver_test)
