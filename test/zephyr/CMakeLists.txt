cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(qoraal_test)

# target_sources(app PRIVATE src/main.c)
set(TEST_SRCS
    ../common/main.c
    ../common/services/wserver_inst.c
    src/platform.c
)
target_sources(app PRIVATE ${TEST_SRCS})

set(QORAAL_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

add_subdirectory(
    ${QORAAL_ROOT}
    ${CMAKE_CURRENT_BINARY_DIR}/qoraal
)

# Suppose the library defined in the repo is called "qoraal"
target_link_libraries(qoraal      PUBLIC zephyr_interface)
target_link_libraries(qoraal-http PUBLIC zephyr_interface)

target_compile_definitions(qoraal PRIVATE CFG_OS_ZEPHYR=1)
target_compile_definitions(qoraal-http PUBLIC CFG_OS_ZEPHYR=1 
                                CFG_HTTPSERVER_TLS_DISABLE=1  
                                CFG_HTTPCLIENT_TLS_DISABLE=1)

target_compile_options(app PRIVATE -O0 -g)
target_compile_options(qoraal PRIVATE -O0 -g)
target_compile_options(qoraal-http PRIVATE -O0 -g)


zephyr_linker_sources(SECTIONS src/zephyr.ld)

target_link_libraries(app PRIVATE qoraal qoraal-http)
