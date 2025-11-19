add_library(core STATIC
    src/app_config.cpp
)

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(core
    PUBLIC
        Qt6::Core
)
