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
add_library(qoraal-http OBJECT ${FLASH_SRC})

# Set target include dirs
target_include_directories(qoraal PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../include)
