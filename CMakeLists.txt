cmake_minimum_required(VERSION 3.14)
project(lowpower_robot VERSION 1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Set up AUTOMOC and some sensible defaults for runtime execution
# When using Qt 6.3, you can replace the code block below with
# qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia Network Test)


file(GLOB COMMON_SOURCES Utils/*.cpp Recorder/*.cpp)
file(GLOB COMMON_HEADERS Utils/*.h Recorder/*.h)

# test
option(TEST "启用测试" OFF)
option(DEBUG_PROCESS "测试音频前处理" OFF)
set(CONFIG_PATH "$ENV{HOME}/.config/lowpower_robot")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_HEADER_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/include) 
set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/software/lowpower_robot")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

add_library(lpcommon SHARED ${COMMON_SOURCES} ${COMMON_HEADERS})
target_compile_definitions(lpcommon PRIVATE LPCOMMON_LIBRARY)
add_executable(lowpower_robot WIN32 MACOSX_BUNDLE
    robot.cpp robot.h
    main.cpp
)
set_target_properties(lowpower_robot PROPERTIES LINK_FLAGS "-Wl,-rpath=$ORIGIN")
macro(install_depend target_name )
    if(NOT TARGET TARGET_${target_name})
        get_target_property(DLLS ${target_name} IMPORTED_LOCATION)
        add_custom_target(TARGET_${target_name} ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DLLS} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
    endif()
endmacro(install_depend)

macro(add_custom_file target_name PATH)
    add_custom_target(TARGET_${target_name}_file ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${PATH} ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
endmacro(add_custom_file)

macro(add_custom_lib custom_lib_name)
    find_package(${custom_lib_name} REQUIRED)
    if(${LIB_NAME}_FOUND)
        target_include_directories(lowpower_robot PRIVATE ${${LIB_NAME}_INCLUDE_DIRS})
        target_link_libraries(lowpower_robot PRIVATE ${${LIB_NAME}_LIBRARIES})
        install_depend(${LIB_NAME})
    else()
        message(FATAL_ERROR "${custom_lib_name} not found")
    endif()
endmacro(add_custom_lib)

target_link_libraries(lpcommon PRIVATE
    Qt::Core
    Qt::Multimedia
    Qt::Network
)

find_package(openssl  REQUIRED)
add_custom_file(openssl ${${LIB_NAME}_ROOT_DIR}/lib/lib*)

target_link_libraries(lowpower_robot PRIVATE
    Qt::Core
    Qt::Multimedia
    Qt::Network
    Qt::Test
    ${openssl_LIBRARIES}
    lpcommon
)

add_subdirectory(Conversation)
add_subdirectory(Wakeup)
add_subdirectory(Plugins)
if(TEST)
    target_sources(lowpower_robot PUBLIC
        Test/tst_sherpa.h Test/TestPluginHelper.h
    )
endif()
if(DEBUG_PROCESS)
    target_compile_definitions(lowpower_robot  PUBLIC -DDEBUG_PROCESS)
endif()

file(COPY Data/ DESTINATION ${CONFIG_PATH}/Data)

install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
    DESTINATION ${CMAKE_INSTALL_PREFIX}
    USE_SOURCE_PERMISSIONS
)