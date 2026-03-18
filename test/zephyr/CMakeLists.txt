cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_CONF_FILE "secrets.conf")


set(QORAAL_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../)

list(APPEND ZEPHYR_EXTRA_MODULES ${QORAAL_ROOT}/qoraal)
list(APPEND ZEPHYR_EXTRA_MODULES ${QORAAL_ROOT}/qoraal-http)


find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(qoraal_test)


# target_sources(app PRIVATE src/main.c)
set(TEST_SRCS
    ../common/main.c
    ../common/services/wserver_inst.c
    src/platform.c

)
target_sources(app PRIVATE ${TEST_SRCS})

target_include_directories(app PRIVATE 
        ${ZEPHYR_QORAAL_MODULE_DIR}/include 
        ${ZEPHYR_QORAAL_HTTP_MODULE_DIR}/include
)