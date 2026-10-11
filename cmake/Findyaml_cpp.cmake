# Findyaml_cpp.cmake
# 定位仓库自带的 yaml-cpp 静态库（lib/yaml-cpp/{include,lib}）。
#
# 项目约定：第三方库随仓库放在 lib/<name>/{include,lib}，由本文件负责产出
# 标准变量，不依赖系统安装。yaml-cpp 必须以 **静态库** 方式链接，
# 因此使用方必须同时定义 YAML_CPP_STATIC_DEFINE（见 dll.h），
# 否则头文件会按 __declspec(dllimport) 展开，导致 LNK2019。
#
# 产出：
#   yaml_cpp_FOUND
#   yaml_cpp_INCLUDE_DIR
#   yaml_cpp_LIBRARY
#   yaml_cpp_LIBRARIES
# 并附带一个 INTERFACE 目标：yaml_cpp::yaml_cpp

include(FindPackageHandleStandardArgs)

# 注意：不能用 QSA_HOME —— 顶层 CMakeLists.txt 把它设成了 $ENV{USERPROFILE}
# （那是运行期配置目录的根），不是仓库根。这里必须独立推导仓库根：
# 本文件位于 <repo>/cmake/ 下，因此上级目录即仓库根。
get_filename_component(_yaml_cpp_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(_yaml_cpp_root "${_yaml_cpp_repo_root}/lib/yaml-cpp")

if(NOT EXISTS "${_yaml_cpp_root}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}yaml-cpp${CMAKE_STATIC_LIBRARY_SUFFIX}"
   AND NOT EXISTS "${_yaml_cpp_root}/lib/yaml-cpp.lib")
    message(WARNING "yaml-cpp not found under ${_yaml_cpp_root}. "
                    "Run scripts/setup.py or restore lib/yaml-cpp/")
endif()

find_path(yaml_cpp_INCLUDE_DIR
    NAMES yaml-cpp/yaml.h
    HINTS "${_yaml_cpp_root}/include"
    NO_DEFAULT_PATH
)

find_library(yaml_cpp_LIBRARY
    NAMES yaml-cpp
    HINTS "${_yaml_cpp_root}/lib"
    NO_DEFAULT_PATH
)

find_package_handle_standard_args(yaml_cpp
    REQUIRED_VARS yaml_cpp_LIBRARY yaml_cpp_INCLUDE_DIR
)

if(yaml_cpp_FOUND)
    set(yaml_cpp_LIBRARIES "${yaml_cpp_LIBRARY}")

    if(NOT TARGET yaml_cpp::yaml_cpp)
        add_library(yaml_cpp::yaml_cpp UNKNOWN IMPORTED)
        set_target_properties(yaml_cpp::yaml_cpp PROPERTIES
            IMPORTED_LOCATION "${yaml_cpp_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${yaml_cpp_INCLUDE_DIR}"
            # 静态链接：屏蔽 dllimport，暴露真实符号
            INTERFACE_COMPILE_DEFINITIONS "YAML_CPP_STATIC_DEFINE"
        )
    endif()
endif()

mark_as_advanced(yaml_cpp_INCLUDE_DIR yaml_cpp_LIBRARY)
