# Collect all test source files
set(TEST_SRCS
    main.c
    services/shell.c
    services/system.c
    services/www/wserver_inst.c
    services/www/css/wcss.c
    services/www/html/wabout.c
    services/www/html/wnlog.c
    services/www/html/wrtlog.c
    services/www/html/wservices.c
    services/www/html/wshell.c
    services/www/html/wsystem.c
    services/www/image/wimage.c
    platform/platform.c
)

# Add the executable target
add_executable(httptest ${TEST_SRCS})

# Link the qoraal library and pthread
target_link_libraries(httptest qoraal qoraal-http pthread ws2_32)
target_compile_options(httptest PRIVATE -O0 -g)
target_compile_options(qoraal PRIVATE -O0 -g)
target_compile_options(qoraal-http PRIVATE -O0 -g)

target_link_options(httptest PRIVATE
    "-Wl,--whole-archive"
    "$<TARGET_FILE:qoraal>"
    "$<TARGET_FILE:qoraal-http>"
    "-Wl,--no-whole-archive"
)

# Generate a map file
set_target_properties(httptest PROPERTIES LINK_FLAGS "-Wl,-Map=output.map -T ${CMAKE_SOURCE_DIR}/test/platform/posix.ld")