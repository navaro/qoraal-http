# Collect all source files
set(HTTP_SRC
    qoraal.c
    httpclient.c
    httpdwnld.c
    httpserver.c
    httpwebapi.c
    httpparse.c
    wserver_m.c
    network.c
    json/frozen.c
    json/jsmn.c
    json/jsmn_tools.c
    mbedtls/mbedtlsutils.c
    qshell/httpcmds.c
)

# Add the library target
add_library(qoraal-http STATIC ${HTTP_SRC})

# Set compile-time definition CFG_OS_FREERTOS for the qoraal target
target_compile_definitions(qoraal-http PRIVATE CFG_OS_FREERTOS)

# Optional: Set include directories for the library if needed
target_include_directories(qoraal-http PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Add ../include as a PUBLIC include directory
target_include_directories(qoraal-http PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Link necessary libraries
# target_link_libraries(qoraal-http qoraal)

# Inherit project-wide includes from sdk_intf_lib
target_link_libraries(qoraal-http PUBLIC sdk_intf_lib qoraal)