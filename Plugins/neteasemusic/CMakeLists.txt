cmake_minimum_required(VERSION 3.14)

project(NeteaseMusic VERSION 0.1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)

# ---------------------------------------------------------------------------
# 两种实现方式：
#   1. NETEASE_USE_JS=ON  —— 插件通过 HTTP 访问本地 node 服务
#      (NeteaseCloudMusicApi)，构建期无额外依赖，**Windows 默认使用该模式**。
#   2. NETEASE_USE_JS=OFF —— FetchContent 拉取 QCloudMusicApi C++ 库源码并参与
#      构建。依赖 git 与子模块，且需要在本地编译出该库，Windows 上不适用。
# ---------------------------------------------------------------------------
if(WIN32)
    option(NETEASE_USE_JS "use node js api" ON)
else()
    option(NETEASE_USE_JS "use node js api" OFF)
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
# JS 模式下用 QNetworkAccessManager 直接请求本地 node 服务，需要 Qt Network；
# Gui 用于把官方模式的登录二维码渲染成 PNG（qrcode.cpp）。
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia Network Gui)
PLUGIN_COMMON()
# 这里统一用带作用域关键字的写法：QSA_LINK_RSA() 内部用的是 PRIVATE，
# CMake 要求同一目标的 target_link_libraries 不能混用 plain 与关键字形式。
target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Multimedia 
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Gui
    lpcommon)

# ---------------------------------------------------------------------------
# 本目录下始终会编译 neteaseofficialclient.cpp（FILE(GLOB "*.cpp") 会自动带上），
# 它实现了官方开放平台的 RSA-SHA256 签名：Windows 走系统 CNG(BCrypt)，
# 其它平台走 OpenSSL。这里只负责把对应的库接上。
# 插件运行期可用 netease_cloud.apiMode 在 node / official 两种实现间切换。
# ---------------------------------------------------------------------------
QSA_LINK_RSA(${PROJECT_NAME})

if(NETEASE_USE_JS)
    message(STATUS "netease use js")
    target_compile_definitions(${PROJECT_NAME} PUBLIC NETEASE_USE_JS)
    find_file(NeteaseAPP app.js
        HINTS NeteaseCloudMusicApi/
    )
    if(NOT NeteaseAPP)
        if(WIN32)
            # app.js 是**运行期**依赖(由配置项 netease_cloud.homeDir 指定)，
            # 不参与编译。这里只提示如何安装，不阻断构建。
            message(STATUS "netease: app.js not found. Run "
                           "scripts/install_netease_js.ps1 to install the node API "
                           "server, then set netease_cloud.homeDir in config.json")
        else()
            execute_process(COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/install_neteasejs.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/scripts)
        endif()
    endif()
else()
    include(FetchContent)
    FetchContent_Declare(QCloudMusicApi
                        GIT_REPOSITORY https://github.com/xinhecuican/QCloudMusicApi.git
                        GIT_TAG dev
                        SOURCE_DIR ${CMAKE_SOURCE_DIR}/lib/QCloudMusicApi)
    fetchcontent_makeavailable(QCloudMusicApi)

    target_include_directories(${PROJECT_NAME} PRIVATE ../../lib/QCloudMusicApi/QCloudMusicApi)
    target_link_libraries(${PROJECT_NAME} QCloudMusicApi)
endif()
