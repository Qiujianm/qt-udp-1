add_library(network STATIC
    src/ws_channel.cpp
    src/frame.cpp
)

target_sources(network
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/network/ws_channel.hpp
)

target_include_directories(network
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(network
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::WebSockets
)
