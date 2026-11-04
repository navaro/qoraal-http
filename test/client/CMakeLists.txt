# Host unit test of the HTTP client. It needs no network and no mbedTLS:
# the socket and mbedTLS calls of httpclient.c are faked in the test.
#
#   cmake -S test/client -B build/client-test
#   cmake --build build/client-test
#   ctest --test-dir build/client-test --output-on-failure
#
# On Windows with MinGW add -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc.
# httpclient_test reads over a plain TCP connection, httpclient_tls_test
# over a fake TLS one. Only the qoraal headers are used; QORAAL_DIR points
# to the checkout, by default the one next to this repository.

cmake_minimum_required(VERSION 3.16)
project(httpclient_test C)

set(QORAAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../qoraal" CACHE PATH "qoraal checkout, for its headers")
if(NOT EXISTS "${QORAAL_DIR}/include/qoraal/qoraal.h")
    message(FATAL_ERROR "No qoraal headers in ${QORAAL_DIR}, set QORAAL_DIR")
endif()

set(HTTP_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")

enable_testing()

function(add_httpclient_test name tls)
    add_executable(${name} test_httpclient.c ${HTTP_DIR}/src/httpparse.c)
    # fake/ comes first, so the TLS build gets the fake mbedtlsutils.h.
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/fake
        ${HTTP_DIR}/include
        ${QORAAL_DIR}/include)
    target_compile_definitions(${name} PRIVATE CFG_OS_POSIX TEST_TLS=${tls})
    if(NOT tls)
        target_compile_definitions(${name} PRIVATE CFG_HTTPCLIENT_TLS_DISABLE=1)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(test_httpclient.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
        target_compile_options(${name} PRIVATE -g)
    endif()
    if(WIN32)
        target_link_libraries(${name} ws2_32)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_httpclient_test(httpclient_test 0)
add_httpclient_test(httpclient_tls_test 1)
