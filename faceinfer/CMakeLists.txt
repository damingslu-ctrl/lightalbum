# faceinfer.exe —— SCRFD + 对齐嵌入 ONNX 独立执行器（第四轮；与 native/ Qt 父进程完全分离，
# 仅 stdio + ORT C API）。工程模板 = inpaint/CMakeLists.txt（MinGW -static + ort_mingw_shim.h）。
# 用法：
#   cmake -S faceinfer -B faceinfer/build -G "MinGW Makefiles" -DONNXRUNTIME_ROOT="D:/la-tools/onnxruntime-win-x64-1.19.2"
#   cmake --build faceinfer/build -j 8
# 要点（inpaint 判例继承）：
#   * ORT zip 发行包【不】带 CMake config 文件，find_package(onnxruntime CONFIG) 必失败，
#     按 ONNXRUNTIME_ROOT 直接引用 include/ 与 lib/onnxruntime.dll。
#   * MinGW -static 折叠 libwinpthread/libgcc/libstdc++，终端用户机无 Qt 运行时也能起。
#   * ORT 头按 MSVC 方言写（_stdcall/SAL），MinGW 用前置 shim 兜平。
#   * faceinfer_stub（#75 门装置）零 ORT 依赖——纯 stdio 独立目标，与真壳共享 AiProto.h。
cmake_minimum_required(VERSION 3.21)
project(faceinfer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ONNXRUNTIME_ROOT "" CACHE PATH "onnxruntime win-x64 package root (contains include/ and lib/)")
if(NOT ONNXRUNTIME_ROOT)
    message(FATAL_ERROR "ONNXRUNTIME_ROOT is required: path to the extracted onnxruntime-win-x64 zip")
endif()

# 协议真源共享：native/src/core/AiProto.h（无 Qt 依赖，MinGW 独立工程可直接 include）。
set(LA_NATIVE_SRC "${CMAKE_CURRENT_LIST_DIR}/../native/src")

# ---- faceinfer 真壳：T1 = 骨架（READY 0 + ERR nomodel），ORT 接线就绪留 T2/T3 填充 ----
add_executable(faceinfer faceinfer.cpp)
target_include_directories(faceinfer PRIVATE "${ONNXRUNTIME_ROOT}/include" "${LA_NATIVE_SRC}")

if(MSVC)
    target_link_libraries(faceinfer PRIVATE "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    target_compile_options(faceinfer PRIVATE /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(faceinfer PRIVATE "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
    target_link_options(faceinfer PRIVATE -static)
    target_compile_options(faceinfer PRIVATE -include "${CMAKE_CURRENT_LIST_DIR}/ort_mingw_shim.h")
else()
    message(FATAL_ERROR "Unsupported toolchain: use MSVC or MinGW g++ (see inpaint/BUILD.md 同款口径)")
endif()

# 构建后把 ORT 动态库拷到 exe 旁（发布装配 T9 前，开发树就地可跑）。
file(GLOB _ort_dlls "${ONNXRUNTIME_ROOT}/lib/onnxruntime*.dll")
foreach(_dll ${_ort_dlls})
    add_custom_command(TARGET faceinfer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" $<TARGET_FILE_DIR:faceinfer>)
endforeach()

# ---- faceinfer_stub：#75 门 hermetic 装置，零 ORT（纯 stdio/winmm 级依赖）----
add_executable(faceinfer_stub faceinfer_stub.cpp)
target_include_directories(faceinfer_stub PRIVATE "${LA_NATIVE_SRC}")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(faceinfer_stub PRIVATE -static)
elseif(MSVC)
    target_compile_options(faceinfer_stub PRIVATE /utf-8)
endif()

# 开发树投递：门（runFaceAiQa）沿用 EraseRunner::findExe 的 dev-fallback 口径，
# 候选 1 = native/build/faceinfer_stub.exe（扁平）。configure 时该目录存在才挂拷贝腿，
# 保证全新克隆（无 native/build）不因缺目录而构建失败；build 树副本始终是权威源。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../native/build/CMakeCache.txt")
    add_custom_command(TARGET faceinfer_stub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:faceinfer_stub>
                "${CMAKE_CURRENT_LIST_DIR}/../native/build/")
endif()
