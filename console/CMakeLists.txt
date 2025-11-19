qt_add_executable(console_app
    src/main.cpp
    src/main_window.cpp
    src/client_details_dialog.cpp
    src/client_discovery.cpp
    src/console_control_server.cpp
    src/console_broadcaster.cpp
    src/database_integration.cpp
    src/jpeg_receiver.cpp
)

target_sources(console_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/console/main_window.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/console/client_details_dialog.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/console/client_discovery.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/console/console_control_server.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/console/console_broadcaster.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/include/console/jpeg_receiver.hpp
)

target_include_directories(console_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 查找FFmpeg库（用于视频播放）
set(FFMPEG_DIR "C:/ffmpeg-shared/ffmpeg-8.0-full_build-shared")
if(EXISTS "${FFMPEG_DIR}")
    set(FFMPEG_INCLUDE_DIR "${FFMPEG_DIR}/include")
    set(FFMPEG_LIB_DIR "${FFMPEG_DIR}/lib")
    
    find_library(AVFORMAT_LIB avformat PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
    find_library(AVCODEC_LIB avcodec PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
    find_library(AVUTIL_LIB avutil PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
    find_library(SWSCALE_LIB swscale PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
    
    if(AVFORMAT_LIB AND AVCODEC_LIB AND AVUTIL_LIB AND SWSCALE_LIB)
        target_include_directories(console_app PRIVATE ${FFMPEG_INCLUDE_DIR})
        message(STATUS "Found FFmpeg libraries for video playback")
    else()
        message(WARNING "FFmpeg libraries not found, video playback may not work")
    endif()
endif()

target_link_libraries(console_app
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::WebSockets
        Qt6::Multimedia
        Qt6::Sql
        Qt6::MultimediaWidgets
        core
        network
)

# 链接FFmpeg库（如果找到）
if(AVFORMAT_LIB AND AVCODEC_LIB AND AVUTIL_LIB AND SWSCALE_LIB)
    target_link_libraries(console_app PRIVATE
        ${AVFORMAT_LIB}
        ${AVCODEC_LIB}
        ${AVUTIL_LIB}
        ${SWSCALE_LIB}
    )
endif()

set_target_properties(console_app PROPERTIES OUTPUT_NAME "DesktopConsole")
set_target_properties(console_app PROPERTIES AUTOMOC_COMPILER_PREDEFINES OFF)
