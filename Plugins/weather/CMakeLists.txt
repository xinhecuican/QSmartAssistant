cmake_minimum_required(VERSION 3.14)

project(Weather VERSION 0.1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network Multimedia)
PLUGIN_COMMON()
target_link_libraries(${PROJECT_NAME} Qt5::Core Qt5::Network Qt5::Multimedia lpcommon)
