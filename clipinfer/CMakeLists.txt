# clipinfer.exe —— CLIP ViT-B-32 图像塔 + 判定规则独立子进程（第四轮 T7；与 native/ Qt
# 父进程完全分离，帧协议走 stdio + ORT C++ API；工程模板 = faceinfer/CMakeLists.txt，
# 但 R-15 裁定 clipinfer 全程无 Qt：读盘走 vendored stb_image（magic 嗅探，JPEG/PNG 通解），
# JSON 手工渲染 —— 「终端机零运行时」判例（inpaint）在本工程恢复成立，发布装配腿只需
# 随包带 onnxruntime*.dll，无 windeployqt 腿）。
# 用法：
#   cmake -S clipinfer -B clipinfer/build -G "MinGW Makefiles" ^
#     -DONNXRUNTIME_ROOT="D:/la-tools/onnxruntime-win-x64-1.19.2"
#   cmake --build clipinfer/build -j 8
# 要点（faceinfer/inpaint 判例继承）：
#   * ORT zip 发行包【不】带 CMake config 文件，按 ONNXRUNTIME_ROOT 直接引用
#     include/ 与 lib/onnxruntime.dll。
#   * MinGW -static 折叠 libwinpthread/libgcc/libstdc++；ORT 头按 MSVC 方言写
#     （_stdcall/SAL），复用 faceinfer/ort_mingw_shim.h 前置兜平（-include）。
#   * clipinfer_stub（#76 门装置）零 ORT 零 Qt —— 纯 stdio 独立目标，与真壳共享 AiProto.h。
cmake_minimum_required(VERSION 3.21)
project(clipinfer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ONNXRUNTIME_ROOT "" CACHE PATH "onnxruntime win-x64 package root (contains include/ and lib/)")
if(NOT ONNXRUNTIME_ROOT)
    message(FATAL_ERROR "ONNXRUNTIME_ROOT is required: path to the extracted onnxruntime-win-x64 zip")
endif()

# 协议真源共享：native/src/core/AiProto.h（无 Qt 依赖，MinGW 独立工程可直接 include）。
set(LA_NATIVE_SRC "${CMAKE_CURRENT_LIST_DIR}/../native/src")

add_executable(clipinfer
    clipinfer.cpp
    preproc.h  preproc.cpp
    rules.cpp
    stb_image.h)
target_include_directories(clipinfer PRIVATE "${ONNXRUNTIME_ROOT}/include" "${LA_NATIVE_SRC}")

if(MSVC)
    target_link_libraries(clipinfer PRIVATE "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    target_compile_options(clipinfer PRIVATE /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(clipinfer PRIVATE "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
    target_link_options(clipinfer PRIVATE -static)
    target_compile_options(clipinfer PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/../faceinfer/ort_mingw_shim.h")
else()
    message(FATAL_ERROR "Unsupported toolchain: use MSVC or MinGW g++ (inpaint/faceinfer 同款口径)")
endif()

# 构建后把 ORT 动态库拷到 exe 旁（发布装配 T9 前，开发树就地可跑）。
file(GLOB _ort_dlls "${ONNXRUNTIME_ROOT}/lib/onnxruntime*.dll")
foreach(_dll ${_ort_dlls})
    add_custom_command(TARGET clipinfer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" $<TARGET_FILE_DIR:clipinfer>)
endforeach()

# 开发树投递（#77 门 runClipModelQa 沿用 findExe dev-fallback 口径，候选 1 =
# native/build/ 扁平；ORT dll 必须同 exe 目录才被子进程首序搜索命中）。
# configure 时该目录存在才挂拷贝腿，保证全新克隆（无 native/build）不因缺目录而构建失败。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../native/build/CMakeCache.txt")
    add_custom_command(TARGET clipinfer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:clipinfer>
                "${CMAKE_CURRENT_LIST_DIR}/../native/build/")
    foreach(_dll ${_ort_dlls})
        add_custom_command(TARGET clipinfer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}"
                    "${CMAKE_CURRENT_LIST_DIR}/../native/build/")
    endforeach()
endif()

# ---- clipinfer_stub：#76 门 hermetic 装置，零 ORT 零 Qt（纯 stdio 级依赖）----
add_executable(clipinfer_stub clipinfer_stub.cpp)
target_include_directories(clipinfer_stub PRIVATE "${LA_NATIVE_SRC}")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(clipinfer_stub PRIVATE -static)
elseif(MSVC)
    target_compile_options(clipinfer_stub PRIVATE /utf-8)
endif()

# 开发树投递：#76 门候选 1 = native/build/clipinfer_stub.exe（扁平）。同上 EXISTS 守卫。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../native/build/CMakeCache.txt")
    add_custom_command(TARGET clipinfer_stub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:clipinfer_stub>
                "${CMAKE_CURRENT_LIST_DIR}/../native/build/")
endif()
