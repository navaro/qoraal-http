# Zephyr module integration for Qoraal-http

zephyr_library()

# Includes
zephyr_include_directories(
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../include
    ${ZEPHYR_QORAAL_MODULE_DIR}/include
)

# Core sources (adjust if needed)
zephyr_library_sources(
    ${CMAKE_CURRENT_LIST_DIR}/../src/qoraal.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/httpclient.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/httpdwnld.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/httpserver.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/httpwebapi.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/httpparse.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/wserver_m.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/network.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/json/frozen.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/json/jsmn.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/json/jsmn_tools.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/qshell/httpcmds.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/mbedtls/mbedtlsutils.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/css/wcss.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/html/wabout.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/html/wnlog.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/html/wrtlog.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/html/wservices.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/html/wshell.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/html/wsystem.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/html/wwebapi.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/example/image/wimage.c

)

# Optional compile defs (keep minimal for now)
zephyr_compile_definitions(
    CFG_OS_ZEPHYR=1
)

# Translate Zephyr config → Qoraal config
zephyr_compile_definitions_ifndef(CONFIG_QORAAL_HTTP_SERVER_TLS CFG_HTTPSERVER_TLS_DISABLE=1)
zephyr_compile_definitions_ifndef(CONFIG_QORAAL_HTTP_CLIENT_TLS CFG_HTTPCLIENT_TLS_DISABLE=1)



