cmake_minimum_required(VERSION 3.14)
project(QSmartAssistant VERSION 1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Set up AUTOMOC and some sensible defaults for runtime execution
# When using Qt 6.3, you can replace the code block below with
# qt_standard_project_setup()
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia Network Test)

file(GLOB COMMON_SOURCES Utils/*.cpp Recorder/*.cpp)
file(GLOB COMMON_HEADERS Utils/*.h Recorder/*.h)

# test
option(TEST "启用测试" OFF)
option(DEBUG_PROCESS "测试音频前处理" OFF)
option(DEBUG_BREAKPAD "开启breakpad捕获dump" OFF)
option(SERVER "server" OFF)
set(CONFIG_PATH "$ENV{HOME}/.config/QSmartAssistant")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_HEADER_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/include)
set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/software/QSmartAssistant")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

add_library(lpcommon SHARED ${COMMON_SOURCES} ${COMMON_HEADERS})
target_compile_definitions(lpcommon PRIVATE LPCOMMON_LIBRARY)
add_executable(QSmartAssistant WIN32 MACOSX_BUNDLE
    robot.cpp robot.h
    main.cpp
)
set_target_properties(QSmartAssistant PROPERTIES LINK_FLAGS "-Wl,-rpath=$ORIGIN")

macro(install_depend target_name)
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
        target_compile_definitions(QSmartAssistant PUBLIC -DBREAKPAD)
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

find_package(openssl REQUIRED)
add_custom_file(openssl ${${LIB_NAME}_ROOT_DIR}/lib/lib*)

target_link_libraries(QSmartAssistant PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Test
    ${openssl_LIBRARIES}
    lpcommon
)

add_subdirectory(Conversation)
add_subdirectory(Wakeup)
add_subdirectory(Plugins)

if(SERVER)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS HttpServer Concurrent)
    target_link_libraries(QSmartAssistant PRIVATE
        Qt${QT_VERSION_MAJOR}::HttpServer
        Qt${QT_VERSION_MAJOR}::Concurrent
    )
    add_subdirectory(Server)
endif()

if(TEST)
    target_sources(QSmartAssistant PUBLIC
        Test/tst_sherpa.h Test/TestPluginHelper.h
    )
    target_compile_definitions(QSmartAssistant PUBLIC -DTEST)
endif()

if(DEBUG_PROCESS)
    target_compile_definitions(QSmartAssistant PUBLIC -DDEBUG_PROCESS)
endif()

file(COPY Data/ DESTINATION ${CONFIG_PATH}/Data)

install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
    DESTINATION ${CMAKE_INSTALL_PREFIX}
    USE_SOURCE_PERMISSIONS
)
