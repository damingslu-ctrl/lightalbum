cmake_minimum_required(VERSION 3.21)
project(LightAlbumNative VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(LA_SOURCES
    src/main.cpp
)

add_executable(LightAlbum WIN32 ${LA_SOURCES})
target_link_libraries(LightAlbum PRIVATE Qt6::Widgets)

# P1 起按模块加入（见 docs/REWRITE-PLAN.md 模块映射）
# add_subdirectory(src/gallery)
# add_subdirectory(src/map)
# add_subdirectory(src/retouch)
# add_subdirectory(src/engine/lumen)
