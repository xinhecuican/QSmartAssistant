target_sources(QSmartAssistant PUBLIC
    Plugin.h
    pluginmanager.cpp pluginmanager.h
)

target_sources(lpcommon PUBLIC IPluginHelper.h)
# 注意：.cpp 必须用 PRIVATE。用 PUBLIC 会把它作为 INTERFACE_SOURCES 传播给所有
# 链接 lpcommon 的目标(如 QSmartAssistant)，导致同一份定义被编译进 exe，
# 与 lpcommon.dll 中导出的符号冲突 → LNK2005 多重定义。
target_sources(lpcommon PRIVATE IPluginHelper.cpp)
list(APPEND COMMON_HEADERS IPluginHelper.h)
set(PLUGIN_PATH ${CONFIG_PATH}/plugins/lib)
option(BUILD_PLUGINS "build plugins" ON)
option(BUILD_PLUGIN_NETEASEMUSIC "build netease music plugin" ON)

# 默认全部构建。NeteaseMusic 在 Windows 下走 NETEASE_USE_JS 模式（依赖运行期的
# node 服务，不依赖构建期的 git/子模块），因此不再默认跳过。如需关闭：
#   -DBUILD_PLUGIN_NETEASEMUSIC=OFF
set(QSA_PLUGIN_SKIP "")
if(NOT BUILD_PLUGIN_NETEASEMUSIC)
    list(APPEND QSA_PLUGIN_SKIP "neteasemusic")
endif()

# 获取目录下的子目录名
# subdirnamelist 子目录名称列表，输出变量，调用处定义，填充时不能加${}
# targetdir      目标路径，为全路径
macro(SUBDIRNAMELIST_MACRO subdirnamelist targetdir)
    # message(STATUS "macro subdirnamelist = ${subdirnamelist}")
    # message(STATUS "macro targetdir = ${targetdir}")
    file(GLOB children ${targetdir}/*) # 获取目标路径下的内容，深度为1
    # message(STATUS "macro children = ${children}")
    set(dirlist "")
    foreach(child ${children})
        file(RELATIVE_PATH child_name ${targetdir} ${child}) # 通过相对路径计算获取名称
        # message(STATUS "macro child = ${child_name}")
        # message(STATUS "targetdir/child = ${targetdir}/${child_name}")
        if(IS_DIRECTORY ${targetdir}/${child_name})
            # message(STATUS "yes dir : targetdir/child = ${targetdir}/${child_name}")
            list(APPEND dirlist ${child_name})
        endif()
    endforeach()
    list(APPEND ${subdirnamelist} ${dirlist})
    # message(STATUS "macro dirlist = ${dirlist}")
    # message(STATUS "macro subdirnamelist = ${subdirnamelist}")
endmacro()
 
# 获取目录下是否有CMakeLists
# hascmakelist 是否含有CMakeLists.txt，输出变量，调用处定义，填充时不能加${}
# targetdir    目标路径，为全路径
macro(CHECK_DIR_HAS_CMAKELIST hascmakelist targetdir)
    # message(STATUS "macro check has cmakelist targetdir = ${targetdir}")
    set(${hascmakelist} FALSE)
    if(IS_DIRECTORY ${targetdir})
        # message(STATUS "macro check has cmakelist is dir, targetdir = ${targetdir}")
        if(EXISTS ${targetdir}/CMakeLists.txt)
            set(${hascmakelist} TRUE)
        endif()
    else()
        message(FATAL_ERROR "Invalid dir para: targetdir = ${targetdir}")
    endif()
endmacro()
 
# 为含有CMakeList的子目录添加add_subdirectory调用
# 自动识别调用处所在目录有哪些子目录，并为还有CMakeLists.txt的子目录添加add_sundirectory调用
macro(ADD_ALL_SUBDIRECTORY)
    set(subdirnamelisttemp "")
    SUBDIRNAMELIST_MACRO(subdirnamelisttemp ${CMAKE_CURRENT_SOURCE_DIR})
    # message(STATUS "macro add_subdir subdirnamelisttemp = ${subdirnamelisttemp}")
    foreach(subdir ${subdirnamelisttemp})
        if(subdir IN_LIST QSA_PLUGIN_SKIP)
            message(STATUS "skip plugin: ${subdir}")
            continue()
        endif()
        # message(STATUS "macro add_subdir subdir = ${subdir}")
        set(hascmakelisttemp FALSE)
        CHECK_DIR_HAS_CMAKELIST(hascmakelisttemp ${CMAKE_CURRENT_SOURCE_DIR}/${subdir})
        # message(STATUS "macro add_subdir hascmakelisttemp = ${hascmakelisttemp}")
        if(${hascmakelisttemp})
            # message(STATUS "macro add_subdir go to add_subdir = ${subdir}")
            add_subdirectory(${subdir})
        endif()
    endforeach()
endmacro()

macro(PLUGIN_COMMON)
    FILE(GLOB SRC_FILES "*.cpp")
    FILE(GLOB HEAD_FILES "*.h")

    source_group("Head Files" FILES ${HEAD_FILES} )
    source_group("Src Files" FILES ${SRC_FILES} )

    ADD_LIBRARY(${PROJECT_NAME} SHARED ${SRC_FILES} ${HEAD_FILES})
    # 多配置生成器(Visual Studio / Xcode / Ninja Multi-Config)会自动在输出目录后
    # 追加 per-config 子目录，导致插件落到 plugins/lib/RelWithDebInfo 而运行时
    # 加载器找不到。这里显式设置各配置的输出目录，去掉多余的子目录层级。
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${PLUGIN_PATH}"
        RUNTIME_OUTPUT_DIRECTORY "${PLUGIN_PATH}")
    foreach(_qsa_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${_qsa_cfg} "${PLUGIN_PATH}"
            RUNTIME_OUTPUT_DIRECTORY_${_qsa_cfg} "${PLUGIN_PATH}")
    endforeach()
    if(NOT MSVC AND NOT WIN32)
        set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-rpath=$ORIGIN")
    endif()
endmacro()
if(BUILD_PLUGINS)
    message(STATUS "build plugin")
    ADD_ALL_SUBDIRECTORY()
endif()
