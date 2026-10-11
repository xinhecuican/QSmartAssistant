cmake_minimum_required(VERSION 3.14)
project(QSmartAssistant VERSION 1.0 LANGUAGES C CXX)
# Qt 6.4 起，Qt 头文件里写着硬性检查：
#   qcompilerdetection.h:  #error "Qt requires a C++17 compiler"
# 所以这里必须是 17。原来写 14 之所以"看起来没问题"，只是因为 MSVC 默认
# 语言模式已经比 14 新、CMake 又不会为 MSVC 的 "14" 追加 /std 开关 —— 属于
# 撞运气；**GCC/Clang 上 CMake 会显式传 -std=gnu++14**，第一个 Qt 头文件就会
# 编译失败（CI 的 gcc 13 + Qt 6.12、以及 Ubuntu 22.04 部署都必然踩到）。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Set up AUTOMOC and some sensible defaults for runtime execution
# When using Qt 6.3, you can replace the code block below with
# qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# 平台相关的用户目录
#   Linux/macOS : $HOME
#   Windows     : $USERPROFILE (或 $HOMEDRIVE$HOMEPATH)
# 统一使用正斜杠，保证和运行时代码中的 QDir::homePath() + "/.config/..." 一致。
# ---------------------------------------------------------------------------
if(WIN32)
    if(DEFINED ENV{USERPROFILE})
        set(QSA_HOME "$ENV{USERPROFILE}")
    elseif(DEFINED ENV{HOMEDRIVE} AND DEFINED ENV{HOMEPATH})
        set(QSA_HOME "$ENV{HOMEDRIVE}$ENV{HOMEPATH}")
    else()
        message(FATAL_ERROR "无法确定用户目录，请设置环境变量 USERPROFILE")
    endif()
else()
    set(QSA_HOME "$ENV{HOME}")
endif()
file(TO_CMAKE_PATH "${QSA_HOME}" QSA_HOME)

if(WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib" ".dll" ${CMAKE_FIND_LIBRARY_SUFFIXES})
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
if(WIN32)
    # Windows 构建要求 Qt 6.8.4 及以上
    if(NOT QT_VERSION_MAJOR EQUAL 6)
        message(FATAL_ERROR "Windows 构建需要 Qt6 (>= 6.8.4)，当前找到的是 Qt${QT_VERSION_MAJOR}")
    elseif(QT_VERSION VERSION_LESS "6.8.4")
        message(FATAL_ERROR "Windows 构建需要 Qt 6.8.4 及以上，当前为 ${QT_VERSION}")
    else()
        message(STATUS "Qt6 ${QT_VERSION} 满足 >= 6.8.4 的要求")
    endif()
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia Network Gui)
message(STATUS "Qt${QT_VERSION_MAJOR} found")

# LLM 框架放在 Utils/LLM/ 子目录（MCP client / Skill registry / 后端 / 会话），
# 因此 glob 要显式带上子目录，不能只写 Utils/*.cpp
file(GLOB COMMON_SOURCES Utils/*.cpp Utils/LLM/*.cpp Recorder/*.cpp)
file(GLOB COMMON_HEADERS Utils/*.h Utils/LLM/*.h Recorder/*.h)

# test
option(TEST "启用测试" OFF)
option(DEBUG_PROCESS "测试音频前处理" OFF)
option(DEBUG_BREAKPAD "开启breakpad捕获dump" OFF)
option(SERVER "server" OFF)
option(MQTT "mqtt" OFF)
option(USE_OPENSSL "链接openssl(源码未直接使用，默认关闭)" OFF)

# ---------------------------------------------------------------------------
# 网易云音乐「官方开放平台」接口需要做 RSA(PKCS#1 v1.5) + SHA-256 签名。
#   Windows —— 直接用系统 CNG(BCrypt)，零第三方依赖；
#   其它平台 —— 用 OpenSSL；若没装开发包则降级为「签名不可用」，
#               不影响 node 模式的既有功能，也不会阻断构建。
# ---------------------------------------------------------------------------
if(WIN32)
    set(QSA_RSA_BACKEND "CNG(BCrypt)")
else()
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(QSA_RSA_BACKEND "OpenSSL")
    else()
        set(QSA_RSA_BACKEND "NONE")
        message(WARNING
            "未找到 OpenSSL：网易云官方 API 的 RSA 签名将被禁用。"
            "安装 libssl-dev 后重新 configure 即可启用；node 模式不受影响。")
    endif()
endif()
message(STATUS "netease official api RSA backend: ${QSA_RSA_BACKEND}")

# 给目标接上 RSA 签名后端。必须在 add_subdirectory(Plugins) 之前定义。
macro(QSA_LINK_RSA target)
    if(WIN32)
        target_link_libraries(${target} PRIVATE bcrypt)
    elseif(OpenSSL_FOUND)
        target_link_libraries(${target} PRIVATE OpenSSL::Crypto)
    else()
        target_compile_definitions(${target} PRIVATE NETEASE_OFFICIAL_NO_CRYPTO)
    endif()
endmacro()

set(CONFIG_PATH "${QSA_HOME}/.config/QSmartAssistant")
set(PLUGIN_PATH "${CONFIG_PATH}/plugins/lib")

# ---------------------------------------------------------------------------
# 输出目录：兼容单配置(Ninja/Make)与多配置(Visual Studio)生成器
# ---------------------------------------------------------------------------
if(CMAKE_CONFIGURATION_TYPES)
    # 多配置生成器，例如 "Visual Studio 17 2022"
    set(_qsa_out_cfg "$<CONFIG>")
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif()
    set(_qsa_out_cfg "${CMAKE_BUILD_TYPE}")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${_qsa_out_cfg}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${_qsa_out_cfg}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${_qsa_out_cfg}/lib")
set(CMAKE_HEADER_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${_qsa_out_cfg}/include")
set(CMAKE_INSTALL_PREFIX "${QSA_HOME}/software/QSmartAssistant" CACHE PATH "安装路径")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# 编译器选项
# ---------------------------------------------------------------------------
if(MSVC)
    # 源码包含大量中文注释与字符串字面量，必须显式指定 UTF-8，否则 MSVC 会乱码/报错
    add_compile_options(/utf-8 /bigobj)
    # CMAKE_CXX_FLAGS 为空，CMake 默认的 /EHsc 没带上；而工程里多处使用 try/catch
    # （silerovad / funasr / livekit 唤醒等），缺少异常展开语义会触发 C4530，
    # 抛异常时局部对象不会析构。这里显式补上，等价于 CMake 的默认行为。
    add_compile_options(/EHsc)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    add_compile_options(/wd4244 /wd4267 /wd4311 /wd4996)
endif()

add_library(lpcommon SHARED ${COMMON_SOURCES} ${COMMON_HEADERS})
target_compile_definitions(lpcommon PRIVATE LPCOMMON_LIBRARY)

# ---------------------------------------------------------------------------
# yaml-cpp：配置文件的 yaml 读写依赖
#   ⚠ lib/yaml-cpp **不在仓库里**（lib/ 被 .gitignore 忽略），由
#     scripts/install_yaml_cpp.sh（Windows: .ps1）构建产出；缺失时 configure
#     直接失败，所以 build_{linux.sh,windows.ps1} 会在 configure 前先装它。
#   必须以静态库方式链接 → 由 Findyaml_cpp.cmake 附带
#   INTERFACE_COMPILE_DEFINITIONS=YAML_CPP_STATIC_DEFINE，
#   否则 yaml-cpp/dll.h 会把符号展开成 __declspec(dllimport) 并引发 LNK2019。
# ---------------------------------------------------------------------------
find_package(yaml_cpp REQUIRED)
target_link_libraries(lpcommon PRIVATE yaml_cpp::yaml_cpp)
message(STATUS "yaml-cpp: ${yaml_cpp_LIBRARY}")

# Windows 上 MSVC 不会像 GCC 那样默认导出所有符号。工程里只有部分类带
# LPCOMMON_EXPORT(dllimport/export)，其余(IPluginHelper、AC::WavFileReader 等)
# 依赖自动导出，否则链接 QSmartAssistant.exe 时会报 LNK2019 未解析外部符号。
if(WIN32)
    set_target_properties(lpcommon PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

add_executable(QSmartAssistant
    robot.cpp robot.h
    main.cpp
)
# 控制台程序：Windows 上不要加 WIN32（那会要求 WinMain），macOS 才需要 bundle
if(APPLE)
    set_target_properties(QSmartAssistant PROPERTIES MACOSX_BUNDLE TRUE)
endif()
# rpath 是 GCC/Clang 的链接选项，MSVC 不识别
if(NOT MSVC AND NOT WIN32)
    set_target_properties(QSmartAssistant PROPERTIES LINK_FLAGS "-Wl,-rpath=$ORIGIN")
endif()

macro(install_depend target_name)
    if(NOT TARGET TARGET_${target_name})
        get_target_property(DLLS ${target_name} IMPORTED_LOCATION)
        # IMPORTED_LOCATION 可能是列表(例如 sherpa 的多个库)，或被 Find 模块设为
        # 导入库(.lib)而非运行库。这里只在“单个存在的、非 .lib 的文件”时才拷贝。
        list(LENGTH DLLS _qsa_dlls_count)
        if(_qsa_dlls_count EQUAL 1 AND EXISTS "${DLLS}" AND NOT "${DLLS}" MATCHES "\\.lib$")
            # 必须先建目录：否则 cmake -E copy_if_different 会把目标目录名当成文件名，
            # 生成一个名为 bin 的文件，导致后续 MSB3191 无法创建目录。
            add_custom_target(TARGET_${target_name} ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DLLS}" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
        endif()
    endif()
endmacro(install_depend)

macro(add_custom_file target_name PATH)
    add_custom_target(TARGET_${target_name}_file ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${PATH} "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
endmacro(add_custom_file)

macro(add_custom_lib custom_lib_name)
    find_package(${custom_lib_name} REQUIRED)

    if(${LIB_NAME}_FOUND)
        target_include_directories(QSmartAssistant PRIVATE ${${LIB_NAME}_INCLUDE_DIRS})
        target_link_libraries(QSmartAssistant PRIVATE ${${LIB_NAME}_LIBRARIES})
        install_depend(${LIB_NAME})
    else()
        message(FATAL_ERROR "${custom_lib_name} not found")
    endif()
endmacro(add_custom_lib)

# Find BreakPad
if(DEBUG_BREAKPAD)
    message(STATUS "enable breakpad")
    find_package(breakpad REQUIRED)

    if(NOT ${LIB_NAME}_FOUND)
        execute_process(COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/install_breakpad.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/scripts)
        find_package(breakpad REQUIRED)
    endif()

    if(${LIB_NAME}_FOUND)
        target_compile_definitions(QSmartAssistant PUBLIC BREAKPAD)
        target_include_directories(QSmartAssistant PRIVATE ${${LIB_NAME}_INCLUDE_DIRS})
        target_link_libraries(QSmartAssistant PRIVATE ${${LIB_NAME}_LIBRARIES})
        install_depend(${LIB_NAME})
    else()
        message(FATAL_ERROR "${custom_lib_name} not found")
    endif()

    add_custom_target(dump_symbols
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/dump_symbols.sh ${CMAKE_BUILD_TYPE} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/scripts)
endif()

# End Find BreakPad
target_link_libraries(lpcommon PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::Network
)

# openssl 源码中并未直接引用，Windows 上 Qt 默认使用系统 Schannel，无需 openssl
if(USE_OPENSSL)
    find_package(openssl REQUIRED)
    target_include_directories(QSmartAssistant PRIVATE ${openssl_INCLUDE_DIRS})
    target_link_libraries(QSmartAssistant PRIVATE ${openssl_LIBRARIES})
    if(NOT WIN32)
        add_custom_file(openssl ${openssl_ROOT_DIR}/lib/lib*)
    endif()
endif()

target_link_libraries(QSmartAssistant PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::Network
    # qrcode.cpp 用 QImage 把二维码落成 PNG（扫码登录用）
    Qt${QT_VERSION_MAJOR}::Gui
    lpcommon
)

# ---------------------------------------------------------------------------
# llama.cpp（可选）：进程内直接加载 GGUF 模型，不过 HTTP 服务。
#   lib/llama/include  llama.h / ggml*.h（只有声明，不需要链接）
#   lib/llama/bin      llama.dll 等运行期 DLL
#
# 用 scripts/install_llama.ps1 安装。没装的话 QSA_HAVE_LLAMA 不定义，
# 主程序照常编译 —— 只是少了「本地推理」这一种后端。
#
# 为什么不需要 .lib 导入库：LlamaApi 在运行期用 GetProcAddress 绑定符号，
# 所以 llama.cpp 对构建完全没有硬依赖。
# ---------------------------------------------------------------------------
# 默认关闭：本地推理后端只在显式需要时启用（依赖 lib/llama 的存在性，
# 与默认 NLU 后端 onnx / 唤醒 livekit 无关）。开启后若 lib/llama 不存在
# 也只是自动退化，不会报错。
option(LLM_LOCAL "enable in-process llama.cpp backend" OFF)

set(QSA_LLAMA_DIR ${PROJECT_SOURCE_DIR}/lib/llama)
if(LLM_LOCAL AND EXISTS "${QSA_LLAMA_DIR}/include/llama.h")
    # 两个目标都需要：
    #   lpcommon        —— LlamaApi / LlamaCppBackend 的实现
    #   QSmartAssistant —— Conversation/NLU/llmnlu.cpp 里要 qobject_cast 到
    #                      LlamaCppBackend 才能判断后端是否支持同步补全
    foreach(_qsa_tgt lpcommon QSmartAssistant)
        target_include_directories(${_qsa_tgt} PRIVATE "${QSA_LLAMA_DIR}/include")
        target_compile_definitions(${_qsa_tgt} PRIVATE QSA_HAVE_LLAMA)
    endforeach()
    if(WIN32)
        # llama.dll 依赖 ggml.dll 再依赖具体指令集的 ggml-cpu-*.dll，
        # 是一整套，所以整目录拷贝而不是逐个文件。
        add_custom_command(TARGET QSmartAssistant POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QSA_LLAMA_DIR}/bin" "$<TARGET_FILE_DIR:QSmartAssistant>"
            COMMENT "复制 llama.cpp 运行期 DLL")
    endif()
    message(STATUS "llama.cpp: enabled (${QSA_LLAMA_DIR})")
else()
    message(STATUS "llama.cpp: not found, local backend disabled "
                   "(run scripts/install_llama.ps1 to enable)")
endif()

# ---------------------------------------------------------------------------
# Windows: 把第三方运行库 DLL 拷到可执行文件目录
#   lib/onnxruntime/bin、lib/sherpa_onnx/bin 由 scripts/install_*.ps1 生成
#   后出现的目录会覆盖先出现的同名 DLL（sherpa-onnx 自带的 onnxruntime.dll 优先）
# ---------------------------------------------------------------------------
if(WIN32)
    set(QSA_DEP_DLL_DIRS
        "${PROJECT_SOURCE_DIR}/lib/onnxruntime/bin"
        "${PROJECT_SOURCE_DIR}/lib/sherpa_onnx/bin")
    add_custom_command(TARGET QSmartAssistant POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT_DIR=$<TARGET_FILE_DIR:QSmartAssistant>
            "-DDEP_DIRS=${QSA_DEP_DLL_DIRS}"
            -P "${CMAKE_SOURCE_DIR}/cmake/CopyDeps.cmake"
        COMMENT "复制第三方依赖 DLL 到输出目录")
endif()

add_subdirectory(Conversation)
add_subdirectory(Wakeup)
add_subdirectory(Plugins)

# ---------------------------------------------------------------------------
# Windows: 部署 Qt 运行期（DLL + 插件目录）
#
#   为什么必须在构建里做：
#   Qt 的多媒体后端 / TLS 后端 / 平台 / 图片格式**都是「插件」**，只把
#   Qt6Multimedia.dll 拷到 exe 目录是不够的：
#     * 缺 plugins/multimedia  → "No QtMultimedia backends found ... "
#                                QMediaPlayer 变成 "Not available"
#     * 缺 plugins/tls         → "No functional TLS backend was found"
#     * 缺插件专属 Qt 模块 DLL → load plugin "Hass" fail（Hass 用 WebSockets）
#   windeployqt 只扫主 exe 的依赖，上述几条都会漏。
#
#   官方流程 scripts/build_windows.ps1 第 5 步本来就会跑 windeployqt；
#   但任何「裸 cmake --build」都不会。一旦重建（甚至只是删掉 bin/ 里的插件
#   目录）就会出现上面两类故障，且极难联想到是构建问题。
#   所以这里用**纯 CMake** 把关键部分补上：不依赖外部工具，受限环境
#   （沙箱里 windeployqt 拉不起子进程）也能跑，并与 windeployqt 幂等共存。
#
#   部署列表与 scripts/deploy_qt.py 的 wanted 保持一致，两处改动请同步。
#   注意：用 Qt6_DIR 推导目录，而不是 $<TARGET_FILE_DIR:Qt6::Xxx>，
#   因为 WebSockets 等模块可能并未作为顶层 target 被 find_package 引入。
# ---------------------------------------------------------------------------
if(WIN32)
    # Qt6_DIR 形如 <Qt>/lib/cmake/Qt6，往上三层即 Qt 根目录
    get_filename_component(_qt_root "${Qt${QT_VERSION_MAJOR}_DIR}/../../.." ABSOLUTE)
    set(_qt_bin_dir "${_qt_root}/bin")
    set(_qt_plugins_dir "${_qt_root}/plugins")
    set(_qt_exe_dir "$<TARGET_FILE_DIR:QSmartAssistant>")

    # (1) Qt 模块 DLL。Multimedia 给 QMediaPlayer 用；WebSockets 给 Hass 插件用
    #     （插件是运行期 dlopen 的，windeployqt 扫不到）。
    set(_qsa_qt_dlls "")
    foreach(_m Core Gui Network Multimedia WebSockets Svg)
        set(_f "${_qt_bin_dir}/Qt${QT_VERSION_MAJOR}${_m}.dll")
        if(EXISTS "${_f}")
            list(APPEND _qsa_qt_dlls "${_f}")
        else()
            message(STATUS "Qt 运行库缺失（跳过）: ${_f}")
        endif()
    endforeach()

    # (2) Qt 自带（bundled）的 FFmpeg 运行库：ffmpegmediaplugin.dll 直接依赖
    #     avcodec / avformat / avutil / swresample / swscale，缺一个多媒体后端就加载失败。
    file(GLOB _qsa_ffmpeg_dlls
        "${_qt_bin_dir}/av*-*.dll"
        "${_qt_bin_dir}/sw*-*.dll")
    list(APPEND _qsa_qt_dlls ${_qsa_ffmpeg_dlls})

    # (3) 其它运行期辅助 DLL
    foreach(_n d3dcompiler_47 opengl32sw)
        set(_f "${_qt_bin_dir}/${_n}.dll")
        if(EXISTS "${_f}")
            list(APPEND _qsa_qt_dlls "${_f}")
        endif()
    endforeach()

    if(_qsa_qt_dlls)
        add_custom_command(TARGET QSmartAssistant POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_qsa_qt_dlls}
                "${_qt_exe_dir}"
            COMMENT "部署 Qt 运行库 DLL（含 bundled FFmpeg）"
            VERBATIM)
        list(LENGTH _qsa_qt_dlls _qsa_n_dlls)
        message(STATUS "Qt 运行库部署: ${_qsa_n_dlls} 个 DLL")
    endif()

    # (4) Qt 插件子目录。只拷 release 版（跳过同名的 xxxd.dll 调试版）。
    set(_qsa_qt_plugin_dirs
        platforms multimedia tls networkinformation
        imageformats iconengines generic styles)
    foreach(_sub ${_qsa_qt_plugin_dirs})
        set(_src_dir "${_qt_plugins_dir}/${_sub}")
        if(NOT IS_DIRECTORY "${_src_dir}")
            continue()
        endif()
        file(GLOB _cand "${_src_dir}/*.dll")
        set(_pick "")
        foreach(_f ${_cand})
            get_filename_component(_fn "${_f}" NAME)
            string(TOLOWER "${_fn}" _lfn)
            if(_lfn MATCHES "d\\.dll$")
                string(REGEX REPLACE "d\\.dll$" ".dll" _rel "${_lfn}")
                if(EXISTS "${_src_dir}/${_rel}")
                    continue()  # 有 release 版，跳过 debug 版
                endif()
            endif()
            list(APPEND _pick "${_f}")
        endforeach()
        if(_pick)
            add_custom_command(TARGET QSmartAssistant POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${_qt_exe_dir}/${_sub}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_pick}
                    "${_qt_exe_dir}/${_sub}"
                COMMENT "部署 Qt 插件: ${_sub}"
                VERBATIM)
            message(STATUS "Qt 插件部署: ${_sub}")
        endif()
    endforeach()
endif()

if(SERVER)
    message(STATUS "enable server")
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS HttpServer Concurrent WebSockets)
    target_link_libraries(QSmartAssistant PRIVATE
        Qt${QT_VERSION_MAJOR}::HttpServer
        Qt${QT_VERSION_MAJOR}::Concurrent
        Qt${QT_VERSION_MAJOR}::WebSockets
    )
    add_subdirectory(Server)
endif()

if(MQTT)
    message(STATUS "enable mqtt")
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Mqtt)
    target_link_libraries(QSmartAssistant PRIVATE Qt${QT_VERSION_MAJOR}::Mqtt)
    target_compile_definitions(QSmartAssistant PUBLIC MQTT)
    target_sources(QSmartAssistant PUBLIC mqtt.h mqtt.cpp)
endif()

if(TEST)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    target_link_libraries(QSmartAssistant PRIVATE Qt${QT_VERSION_MAJOR}::Test)
    target_sources(QSmartAssistant PUBLIC
        Test/tst_sherpa.h Test/TestPluginHelper.h
    )
    # 官方 API 单测需要直接链接该客户端（插件是独立 DLL，单测跑在可执行文件里）。
    # qrcode 也一并编进来，用于逐位校验自研二维码编码器。
    target_sources(QSmartAssistant PRIVATE
        Plugins/neteasemusic/neteaseofficialclient.h
        Plugins/neteasemusic/neteaseofficialclient.cpp
        Plugins/neteasemusic/qrcode.h
        Plugins/neteasemusic/qrcode.cpp
    )
    QSA_LINK_RSA(QSmartAssistant)
    target_compile_definitions(QSmartAssistant PUBLIC TEST)
endif()

if(DEBUG_PROCESS)
    target_compile_definitions(QSmartAssistant PUBLIC DEBUG_PROCESS)
endif()

file(COPY Data/ DESTINATION ${CONFIG_PATH}/Data)

install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
    DESTINATION ${CMAKE_INSTALL_PREFIX}
    USE_SOURCE_PERMISSIONS
)
