# Collect all source files
set(FLASH_SRC
    qoraal_http.c
)


# Add the library target
add_library(qoraal-http STATIC ${FLASH_SRC})

# Link necessary libraries
target_link_libraries(qoraal-http qoraal)

