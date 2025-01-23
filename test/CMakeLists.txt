# Collect all test source files
set(TEST_SRCS
    main.c
    services/shell.c
    services/lwip.c
    platform/platform.c
)

# Add the executable target
add_executable(lwiptest ${TEST_SRCS})

# Link the qoraal library and pthread
target_link_libraries(lwiptest qoraal qoraal-http pthread ws2_32)
target_compile_options(lwiptest PRIVATE -O0 -g)
target_compile_options(qoraal PRIVATE -O0 -g)
target_compile_options(qoraal-http PRIVATE -O0 -g)

target_link_options(lwiptest PRIVATE
    "-Wl,--whole-archive"
    "$<TARGET_FILE:qoraal>"
    "-Wl,--no-whole-archive"
)

# Generate a map file
set_target_properties(lwiptest PROPERTIES LINK_FLAGS "-Wl,-Map=output.map -T ${CMAKE_SOURCE_DIR}/test/platform/posix.ld")