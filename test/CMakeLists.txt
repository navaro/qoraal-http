# Collect all test source files
set(WSERVER_SRCS
    services/www/wserver_inst.c
    services/www/css/wcss.c
    services/www/html/wabout.c
    services/www/html/wnlog.c
    services/www/html/wrtlog.c
    services/www/html/wservices.c
    services/www/html/wshell.c
    services/www/html/wsystem.c
    services/www/html/wwebapi.c
    services/www/image/wimage.c
)

# Add the library target
add_library(qoraal-wserver STATIC ${WSERVER_SRCS})

# Set compile-time definition CFG_OS_FREERTOS for the qoraal target
target_compile_definitions(qoraal-wserver PRIVATE CFG_OS_FREERTOS)

# Optional: Set include directories for the library if needed
target_include_directories(qoraal-wserver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Add ../include as a PUBLIC include directory
target_include_directories(qoraal-wserver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Link necessary libraries
# target_link_libraries(qoraal-http qoraal)

# Inherit project-wide includes from sdk_intf_lib
target_link_libraries(qoraal-wserver PUBLIC sdk_intf_lib qoraal qoraal-http)