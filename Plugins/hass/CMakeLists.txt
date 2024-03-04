cmake_minimum_required(VERSION 3.14)

project(Hass VERSION 0.1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network)
FILE(GLOB SRC_FILES "*.cpp")
FILE(GLOB HEAD_FILES "*.h")

source_group("Head Files" FILES ${HEAD_FILES} )
source_group("Src Files" FILES ${SRC_FILES} )

ADD_LIBRARY(${PROJECT_NAME} SHARED ${SRC_FILES} ${HEAD_FILES})
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt::Network lpcommon)
set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_PATH})