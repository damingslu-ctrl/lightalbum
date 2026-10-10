# inpaint.exe —— MSVC + ONNX Runtime 独立执行器（与 native/ 的 MinGW 父进程完全分离）。
# 用法与依赖解包见同目录 BUILD.md。要点：
#   cmake -S inpaint -B inpaint/build -A x64 -DONNXRUNTIME_ROOT="C:/dev/onnxruntime-win-x64-1.19.2"
#   cmake --build inpaint/build --config Release
cmake_minimum_required(VERSION 3.21)
project(inpaint LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ONNX Runtime 官方 win-x64 压缩包根目录（含 include/ 与 lib/onnxruntime.lib）。
# 提供后自动进入 CMAKE_PREFIX_PATH，供 ORT 自带的 CMake config 包（onnxruntime::onnxruntime 目标）定位。
set(ONNXRUNTIME_ROOT "" CACHE PATH "onnxruntime win-x64 package root (contains include/ and lib/)")
if(ONNXRUNTIME_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${ONNXRUNTIME_ROOT}")
endif()

# CONFIG 模式：命中 ORT 发行包里的 onnxruntimeConfig.cmake，提供 onnxruntime::onnxruntime 导入目标。
find_package(onnxruntime CONFIG REQUIRED)

add_executable(inpaint inpaint.cpp)
target_link_libraries(inpaint PRIVATE onnxruntime::onnxruntime)

# 源文件含 UTF-8 中文注释：MSVC 必须 /utf-8，否则按本地代码页误读注释/字符串。
target_compile_options(inpaint PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# 构建后把 ORT 动态库拷到 exe 旁（运行期就地加载，零云端、零安装）。
# 静态库发行包（无 dll）时 glob 为空，跳过即可——由 BUILD.md 指导用 zip 包。
if(ONNXRUNTIME_ROOT)
    file(GLOB _ort_dlls "${ONNXRUNTIME_ROOT}/lib/onnxruntime*.dll")
    foreach(_dll ${_ort_dlls})
        add_custom_command(TARGET inpaint POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" $<TARGET_FILE_DIR:inpaint>)
    endforeach()
endif()

# 部署（Task 8 release.ps1 的暂存口径）：
#   inpaint.exe + onnxruntime*.dll + lama.onnx (+LICENSE) → <appdir>/3rdparty/lama/bin/
# 父端 EraseRunner::findExe 第一候选即该目录下的 inpaint.exe。
