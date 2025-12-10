# Collect all test source files
set(TEST_SRCS
    main.c
    platform.c
    services/wserver_inst.c

)

# Add the executable target
add_executable(httptest ${TEST_SRCS})
target_compile_definitions(qoraal-http PUBLIC CFG_HTTPSERVER_TLS_DISABLE=1)
target_compile_definitions(qoraal-http PUBLIC CFG_HTTPCLIENT_TLS_DISABLE=1)
target_compile_definitions(httptest PRIVATE CFG_OS_POSIX)
target_compile_definitions(qoraal PRIVATE CFG_OS_POSIX)
target_compile_definitions(qoraal-http PRIVATE CFG_OS_POSIX)
target_compile_options(httptest PRIVATE -O0 -g)
target_compile_options(qoraal PRIVATE -O0 -g)
target_compile_options(qoraal-http PRIVATE -O0 -g)



# Link the qoraal library and pthread
if(WIN32)
    target_link_libraries(httptest qoraal qoraal-http pthread ws2_32)
else()
    target_link_libraries(httptest qoraal qoraal-http pthread)
endif()

# Generate a map file
set_target_properties(httptest PROPERTIES LINK_FLAGS "-Wl,-Map=output.map -T ${CMAKE_SOURCE_DIR}/test/posix.ld")
