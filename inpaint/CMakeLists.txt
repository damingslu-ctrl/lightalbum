# inpaint.exe —— LaMa ONNX 独立执行器（与 native/ 的 Qt 父进程完全分离，仅 stdio + ORT C API）。
# 用法与依赖解包见同目录 BUILD.md。要点：
#   MinGW（本仓库 QA 主机实测路径，无需 MSVC）:
#     cmake -S inpaint -B inpaint/build -G "MinGW Makefiles" -DONNXRUNTIME_ROOT="D:/onnxruntime-win-x64-1.19.2"
#     cmake --build inpaint/build
#   MSVC:
#     cmake -S inpaint -B inpaint/build -A x64 -DONNXRUNTIME_ROOT="C:/dev/onnxruntime-win-x64-1.19.2"
#     cmake --build inpaint/build --config Release
cmake_minimum_required(VERSION 3.21)
project(inpaint LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ONNX Runtime 官方 win-x64 压缩包根目录（含 include/ 与 lib/onnxruntime.dll|.lib）。
# 注意：该 zip 发行包【不】自带 CMake config 文件（v1.19.2 实测目录只有 include/ 与 lib/），
# find_package(onnxruntime CONFIG) 必失败——历史上本文件用过它，是纸面假设，2026-10-11 修正。
set(ONNXRUNTIME_ROOT "" CACHE PATH "onnxruntime win-x64 package root (contains include/ and lib/)")
if(NOT ONNXRUNTIME_ROOT)
    message(FATAL_ERROR "ONNXRUNTIME_ROOT is required: path to the extracted onnxruntime-win-x64 zip")
endif()

add_executable(inpaint inpaint.cpp)
target_include_directories(inpaint PRIVATE "${ONNXRUNTIME_ROOT}/include")

if(MSVC)
    # MSVC 走正规导入库；源文件含 UTF-8 中文注释，/utf-8 不可删。
    target_link_libraries(inpaint PRIVATE "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    target_compile_options(inpaint PRIVATE /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # MinGW：GNU ld 对链接行上的 DLL 路径自动生成隐式导入（onnxruntime 是稳定 C ABI，
    # 与 MSVC 构建二进制等价）。-static 把 win32 线程模型自带的 libwinpthread/libgcc/libstdc++
    # 依赖折叠进 exe（2026-10-11 objdump 验证仅剩 KERNEL32/msvcrt/onnxruntime 三项导入）——
    # 否则终端用户机器没有 Qt MinGW 运行时，发布包直接起不来。
    target_link_libraries(inpaint PRIVATE "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
    target_link_options(inpaint PRIVATE -static)
    # ORT 头文件在 _WIN32 下按 MSVC 方言写（_stdcall/SAL 注解），MinGW 用前置 shim 兜平。
    target_compile_options(inpaint PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/ort_mingw_shim.h")
else()
    message(FATAL_ERROR "Unsupported toolchain: use MSVC or MinGW g++ (see BUILD.md)")
endif()

# 构建后把 ORT 动态库拷到 exe 旁（运行期就地加载，零云端、零安装）。
file(GLOB _ort_dlls "${ONNXRUNTIME_ROOT}/lib/onnxruntime*.dll")
foreach(_dll ${_ort_dlls})
    add_custom_command(TARGET inpaint POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" $<TARGET_FILE_DIR:inpaint>)
endforeach()

# 部署（Task 8 release.ps1 的暂存口径）：
#   inpaint.exe + onnxruntime*.dll + lama.onnx (+LICENSE) → <appdir>/3rdparty/lama/bin/
# 父端 EraseRunner::findExe 第一候选即该目录下的 inpaint.exe。
