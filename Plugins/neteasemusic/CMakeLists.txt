cmake_minimum_required(VERSION 3.14)

project(NeteaseMusic VERSION 0.1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia)
PLUGIN_COMMON()
target_include_directories(${PROJECT_NAME} PRIVATE ../../lib/netease_music/include)
link_directories(${PROJECT_SOURCE_DIR}/../../lib/netease_music/lib)
set(NETEASE_LIB_DIR ${PROJECT_SOURCE_DIR}/../../lib/netease_music/lib)
find_library(lib_CApi NAMES CApi  HINTS ${NETEASE_LIB_DIR})
find_library(lib_QCloudMusicApi NAMES QCloudMusicApi  HINTS ${NETEASE_LIB_DIR})
find_library(lib_crypto NAMES crypto  HINTS ${NETEASE_LIB_DIR})
find_library(lib_ssl NAMES ssl  HINTS ${NETEASE_LIB_DIR})
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt::Multimedia lpcommon)
target_link_libraries(${PROJECT_NAME}
    ${lib_CApi}
    ${lib_QCloudMusicApi}
    ${lib_crypto}
    ${lib_ssl}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${NETEASE_LIB_DIR}/lib* ${PLUGIN_PATH}
)

# add_custom_file(${PROJECT_NAME} ${NETEASE_LIB_DIR}/lib*)

# file(COPY 
#     ${lib_CApi}
#     ${lib_QCloudMusicApi}
#     ${lib_crypto}
#     ${lib_ssl}
# DESTINATION ${PLUGIN_PATH})
