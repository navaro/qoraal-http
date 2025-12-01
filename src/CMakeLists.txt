# Collect all source files
set(FLASH_SRC
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
    qshell/httpcmds.c
    example/css/wcss.c
    example/html/wabout.c
    example/html/wnlog.c
    example/html/wrtlog.c
    example/html/wservices.c
    example/html/wshell.c
    example/html/wsystem.c
    example/html/wwebapi.c
    example/image/wimage.c
)


# Add the library target
add_library(qoraal-http STATIC ${FLASH_SRC})

# Link necessary libraries
target_link_libraries(qoraal-http qoraal)

