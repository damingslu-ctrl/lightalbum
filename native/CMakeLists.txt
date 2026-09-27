cmake_minimum_required(VERSION 3.21)
project(LightAlbumNative VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)

set(LA_SOURCES
    src/main.cpp
    src/core/Library.h
    src/core/Library.cpp
    src/ui/DarkPalette.h
    src/ui/ThumbCache.h
    src/ui/ThumbCache.cpp
    src/ui/GalleryModel.h
    src/ui/GalleryModel.cpp
    src/ui/GalleryPage.h
    src/ui/GalleryPage.cpp
    src/ui/MapView.h
    src/ui/MapView.cpp
    src/ui/MapPage.h
    src/ui/MapPage.cpp
    src/ui/CullPage.h
    src/ui/CullPage.cpp
    src/ui/RetouchPage.h
    src/ui/RetouchPage.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/core/CullEngine.h
    src/core/CullEngine.cpp
    src/core/RetouchEngine.h
    src/core/RetouchEngine.cpp
)

# P2 地图数据（零外链编进 exe）
set(LA_RESOURCES
    resources.qrc
)

add_executable(LightAlbum WIN32 ${LA_SOURCES} ${LA_RESOURCES})
target_link_libraries(LightAlbum PRIVATE Qt6::Widgets Qt6::Sql)

# P3 起按模块加入（见 docs/REWRITE-PLAN.md 模块映射）
# add_subdirectory(src/retouch)
# add_subdirectory(src/engine/lumen)
