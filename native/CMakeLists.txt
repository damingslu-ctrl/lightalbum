cmake_minimum_required(VERSION 3.21)
project(LightAlbumNative VERSION 2.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent Network)

set(LA_SOURCES
    src/main.cpp
    src/core/Library.h
    src/core/Library.cpp
    src/core/PathGuess.h
    src/core/PathGuess.cpp
    src/core/ExifReader.h
    src/core/ExifReader.cpp
    src/core/Scanner.h
    src/core/Scanner.cpp
    src/core/ICloudSource.h
    src/core/ICloudSource.cpp
    src/core/LumenAgent.h
    src/core/LumenAgent.cpp
    src/core/GeoIndex.h
    src/core/GeoIndex.cpp
    src/core/Vault.h
    src/core/Vault.cpp
    src/ui/DarkPalette.h
    src/ui/ThumbCache.h
    src/ui/ThumbCache.cpp
    src/ui/GalleryModel.h
    src/ui/GalleryModel.cpp
    src/ui/GalleryPage.h
    src/ui/GalleryPage.cpp
    src/ui/GalleryCardPainter.h
    src/ui/GalleryCardPainter.cpp
    src/ui/EditorialView.h
    src/ui/EditorialView.cpp
    src/ui/MapView.h
    src/ui/MapView.cpp
    src/ui/MapPage.h
    src/ui/MapPage.cpp
    src/ui/LogPage.h
    src/ui/LogPage.cpp
    src/ui/CullPage.h
    src/ui/CullPage.cpp
    src/ui/RetouchPage.h
    src/ui/RetouchPage.cpp
    src/ui/MaskCanvas.h
    src/ui/MaskCanvas.cpp
    src/ui/CropCanvas.h
    src/ui/CropCanvas.cpp
    src/ui/CompareWidget.h
    src/ui/CompareWidget.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/HomePage.h
    src/ui/HomePage.cpp
    src/ui/Splash.h
    src/ui/Splash.cpp
    src/core/CullEngine.h
    src/core/CullEngine.cpp
    src/core/RetouchEngine.h
    src/core/RetouchEngine.cpp
    src/core/LumenEngine.h
    src/core/LumenEngine.cpp
    src/core/EraseBridge.h
    src/core/EraseBridge.cpp
)

# P2 地图数据（零外链编进 exe）
set(LA_RESOURCES
    resources.qrc
)

add_executable(LightAlbum WIN32 ${LA_SOURCES} ${LA_RESOURCES})
# P9 起 LumenAgent 走本地 Ollama（仅回环）→ Qt6Network 回归（release.ps1 不再 -TrimNetwork）
# P16 vault 口令学走 Windows BCrypt（PBKDF2-SHA256 + GenRandom）
target_link_libraries(LightAlbum PRIVATE Qt6::Widgets Qt6::Sql Qt6::Concurrent Qt6::Network bcrypt)
# 注入版本号（来自 project(VERSION)），供 main.cpp 在标题/关于中展示
target_compile_definitions(LightAlbum PRIVATE LA_VERSION="${PROJECT_VERSION}")

# P3 起按模块加入（见 docs/REWRITE-PLAN.md 模块映射）
# add_subdirectory(src/retouch)
# add_subdirectory(src/engine/lumen)
