cmake_minimum_required(VERSION 3.21)
project(LightAlbumNative VERSION 2.32.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# 发行构建类型：默认 Release。CMAKE_BUILD_TYPE 对单配置生成器是空串，
# 而空串会让 gcc 不带任何优化/DNDEBUG 标志（实测 build/CMakeCache.txt 一度
# 停在 Debug，release.ps1 便把 47MB 的 Debug exe 与 windeployqt 的 Release Qt
# DLL 混装进同一个发行包）。C8 契约现在会硬性拒绝非 Release 产物。
# 显式 -DCMAKE_BUILD_TYPE=... 仍可覆盖（本地调试用 Debug 即可）。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent Network)

set(LA_SOURCES
    src/main.cpp
    src/core/Library.h
    src/core/Library.cpp
    src/core/PathGuess.h
    src/core/PathGuess.cpp
    src/core/ExifReader.h
    src/core/ExifReader.cpp
    src/core/RawDecode.h
    src/core/RawDecode.cpp
    src/core/VideoDecode.h
    src/core/VideoDecode.cpp
    src/core/ImageLoad.h
    src/core/ImageLoad.cpp
    src/core/VideoDecoder.h
    src/core/VideoDecoder.cpp
    src/core/AudioPlayer.h
    src/core/AudioPlayer.cpp
    src/core/Scanner.h
    src/core/Scanner.cpp
    src/core/ICloudSource.h
    src/core/ICloudSource.cpp
    src/core/qaExit.h
    src/core/qaExit.cpp
    src/core/LumenAgent.h
    src/core/LumenAgent.cpp
    src/core/GeoIndex.h
    src/core/GeoIndex.cpp
    src/core/GeoHotspot.h
    src/core/GeoHotspot.cpp
    src/core/Vault.h
    src/core/Vault.cpp
    src/ui/DarkPalette.h
    src/ui/ThumbCache.h
    src/ui/ThumbCache.cpp
    src/ui/GalleryModel.h
    src/ui/GalleryModel.cpp
    src/ui/GalleryPage.h
    src/ui/GalleryPage.cpp
    src/ui/DedupDialog.h
    src/ui/DedupDialog.cpp
    src/ui/GalleryCardPainter.h
    src/ui/GalleryCardPainter.cpp
    src/ui/VideoPlayerWidget.h
    src/ui/VideoPlayerWidget.cpp
    src/ui/VideoLightbox.h
    src/ui/VideoLightbox.cpp
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
    src/ui/HiDpi.h
    src/ui/HiDpi.cpp
    src/ui/FullCache.h
    src/ui/FullCache.cpp
    src/core/CullEngine.h
    src/core/CullEngine.cpp
    src/core/RetouchEngine.h
    src/core/RetouchEngine.cpp
    src/core/LumenEngine.h
    src/core/LumenEngine.cpp
    src/core/PhotoCluster.h
    src/core/PhotoCluster.cpp
    src/core/StoryGen.h
    src/core/StoryGen.cpp
    src/core/PhotoDedup.h
    src/core/PhotoDedup.cpp
    src/core/Classifier.h
    src/core/Classifier.cpp
    src/core/PersonDetector.h
    src/core/PersonDetector.cpp
    src/core/FaceCluster.h
    src/core/FaceCluster.cpp
    src/core/EraseBridge.h
    src/core/EraseBridge.cpp
)

# P2 地图数据（零外链编进 exe）
set(LA_RESOURCES
    resources.qrc
    i18n.qrc
)

add_executable(LightAlbum WIN32 ${LA_SOURCES} ${LA_RESOURCES})
# P9 起 LumenAgent 走本地 Ollama（仅回环）→ Qt6Network 回归（release.ps1 不再 -TrimNetwork）
# P16 vault 口令学走 Windows BCrypt（PBKDF2-SHA256 + GenRandom）
target_link_libraries(LightAlbum PRIVATE Qt6::Widgets Qt6::Sql Qt6::Concurrent Qt6::Network bcrypt winmm)
# 注入版本号（来自 project(VERSION)），供 main.cpp 在标题/关于中展示
target_compile_definitions(LightAlbum PRIVATE LA_VERSION="${PROJECT_VERSION}")

# [2.24.0] Pre-commit auto-gate is live: native/hooks/pre-commit.ps1 blocks the
# commit on any RED fast-tier gate (and on a stale exe). Rebuild before commit.

# P3 起按模块加入（见 docs/REWRITE-PLAN.md 模块映射）
# add_subdirectory(src/retouch)
# add_subdirectory(src/engine/lumen)

# [2.28.0] RAW 支持：把 LibRaw 预编译产物拷到构建树，使 RawDecode::available()
# 在开发态与打包态走**同一条查找路径**（<exe 同级>/3rdparty/libraw/bin）。
# 仓库零二进制 —— 产物由 fetch_libraw.ps1 下载；未下载时静默跳过，
# RAW 优雅降级为「不导入」（见 RawDecode::isImportable），绝不产生无缩略图空卡片。
set(LA_LIBRAW_BIN "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libraw/bin")
if(EXISTS "${LA_LIBRAW_BIN}/dcraw_emu.exe")
    add_custom_command(TARGET LightAlbum POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LA_LIBRAW_BIN}" "$<TARGET_FILE_DIR:LightAlbum>/3rdparty/libraw/bin"
        COMMENT "Staging LibRaw binaries for RAW decode")
else()
    message(STATUS "LibRaw not fetched -- RAW support degrades to not-imported. Run fetch_libraw.ps1")
endif()

# [2.28.0] 视频支持：把 FFmpeg 预编译产物（ffmpeg.exe + ffprobe.exe）拷到构建树，
# 查找路径与 LibRaw 同构（<exe 同级>/3rdparty/ffmpeg/bin），开发态与打包态一致。
# 注意：ffplay.exe 刻意**不**随包（我们不播放，省约 107MB）。体积巨大（两个 exe 合计
# 约 200MB，全静态无 dll），故发布包会显著变大 —— 这是已知并被接受的取舍。
set(LA_FFMPEG_BIN "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg/bin")
if(EXISTS "${LA_FFMPEG_BIN}/ffmpeg.exe")
    add_custom_command(TARGET LightAlbum POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LA_FFMPEG_BIN}" "$<TARGET_FILE_DIR:LightAlbum>/3rdparty/ffmpeg/bin"
        COMMENT "Staging FFmpeg binaries for video decode")
else()
    message(STATUS "FFmpeg not fetched -- video support degrades to not-imported. Run fetch_ffmpeg.ps1")
endif()

