# faceinfer.exe —— SCRFD + 对齐嵌入 ONNX 独立执行器（第四轮；与 native/ Qt 父进程完全分离的
# 独立进程，帧协议走 stdio + ORT C API；T2 起链最小 Qt6 Core/Gui —— QImageReader 读盘是
# spec §3 verbatim 契约。工程模板仍 = inpaint/CMakeLists.txt（MinGW -static + ort_mingw_shim.h）。
# 用法：
#   cmake -S faceinfer -B faceinfer/build -G "MinGW Makefiles" ^
#     -DONNXRUNTIME_ROOT="D:/la-tools/onnxruntime-win-x64-1.19.2" ^
#     -DCMAKE_PREFIX_PATH="D:/qt/6.8.3/mingw_64"
#   cmake --build faceinfer/build -j 8
# 要点（inpaint 判例继承）：
#   * ORT zip 发行包【不】带 CMake config 文件，find_package(onnxruntime CONFIG) 必失败，
#     按 ONNXRUNTIME_ROOT 直接引用 include/ 与 lib/onnxruntime.dll。
#   * MinGW -static 折叠 libwinpthread/libgcc/libstdc++；但 faceinfer.exe 运行期需
#     Qt6Core.dll/Qt6Gui.dll + imageformats 插件（QImageReader 解 JPEG/PNG）——inpaint
#     「终端机零运行时」判例对 faceinfer 不再成立，发布装配腿（T9/T10）须随包带 Qt 运行时
#     （windeployqt 口径）。faceinfer_stub 仍零 Qt 零 ORT。
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

# ---- faceinfer 真壳：T2 = SCRFD det 会话 + QImageReader 读盘 + --det-selftest 比较核。
# T2 起需 Qt6 Core+Gui（QImageReader/QJsonDocument——spec §3 钉死 QImageReader 读盘口径；
# 只链这两个最小件，无 Widgets/QObject，AUTOMOC 不需要；configure 需
# -DCMAKE_PREFIX_PATH=<Qt mingw_64>，MinGW 同族 ABI 直链，inpaint 的「无 Qt」判例只约束
# 那份 MSVC 工程，不约束本工程——controller brief verbatim）。
add_executable(faceinfer
    faceinfer.cpp
    scrfd.h    scrfd.cpp
    align.h    align.cpp
    imgkit.h   imgkit.cpp)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
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
target_link_libraries(faceinfer PRIVATE Qt6::Core Qt6::Gui)

# 构建后把 ORT 动态库拷到 exe 旁（发布装配 T9 前，开发树就地可跑）。
file(GLOB _ort_dlls "${ONNXRUNTIME_ROOT}/lib/onnxruntime*.dll")
foreach(_dll ${_ort_dlls})
    add_custom_command(TARGET faceinfer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" $<TARGET_FILE_DIR:faceinfer>)
endforeach()

# 开发树投递（T3 扩）：#78 门（runFaceModelQa）沿用 EraseRunner::findExe 的 dev-fallback
# 口径起真 faceinfer.exe——候选 1 = native/build/ 扁平。真壳比桩多两类运行时依赖
# （Qt6 已在 native/build；ORT dll 必须同 exe 目录才被子进程首序搜索命中），
# 故 faceinfer.exe + onnxruntime*.dll 一并 POST_BUILD 拷入 native/build。
# configure 时该目录存在才挂拷贝腿，保证全新克隆（无 native/build）不因缺目录而构建失败。
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../native/build/CMakeCache.txt")
    add_custom_command(TARGET faceinfer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:faceinfer>
                "${CMAKE_CURRENT_LIST_DIR}/../native/build/")
    foreach(_dll ${_ort_dlls})
        add_custom_command(TARGET faceinfer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}"
                    "${CMAKE_CURRENT_LIST_DIR}/../native/build/")
    endforeach()
endif()

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
