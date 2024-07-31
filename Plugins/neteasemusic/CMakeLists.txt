cmake_minimum_required(VERSION 3.14)

project(NeteaseMusic VERSION 0.1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)

option(NETEASE_USE_JS "use node js api" OFF)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia)
PLUGIN_COMMON()
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Multimedia lpcommon)
if(NETEASE_USE_JS)
    message(STATUS "netease use js")
    find_file(NeteaseAPP app.js
        HINTS NeteaseCloudMusicApi/
    )
    if(NOT NeteaseAPP)
        execute_process(COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/install_neteasejs.sh WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/scripts)
    endif()
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DNETEASE_USE_JS)
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

# add_custom_file(${PROJECT_NAME} ${NETEASE_LIB_DIR}/lib*)

# file(COPY 
#     ${lib_CApi}
#     ${lib_QCloudMusicApi}
#     ${lib_crypto}
#     ${lib_ssl}
# DESTINATION ${PLUGIN_PATH})
