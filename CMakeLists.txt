cmake_minimum_required(VERSION 3.20)
project(QtUdpMonitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    WebSockets
    Multimedia
    MultimediaWidgets
    Sql
)

# FFmpeg (可选,如果不需要视频录制可以注释掉)
set(FFMPEG_DIR "C:/ffmpeg-shared/ffmpeg-8.0-full_build-shared" CACHE PATH "FFmpeg directory")
if(EXISTS "${FFMPEG_DIR}")
    message(STATUS "Found FFmpeg at: ${FFMPEG_DIR}")
    include_directories(${FFMPEG_DIR}/include)
    link_directories(${FFMPEG_DIR}/bin)
else()
    message(WARNING "FFmpeg not found at ${FFMPEG_DIR}, video recording features disabled")
endif()

# Include cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 子项目
add_subdirectory(libs)
add_subdirectory(console)
