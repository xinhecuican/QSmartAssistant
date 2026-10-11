# Findyaml_cpp.cmake
# 定位 yaml-cpp（配置文件的 YAML 读写依赖，Qt 没有 YAML 模块）。
#
# 查找顺序：
#   1. **仓库自带**：lib/yaml-cpp/{include,lib}，由 scripts/install_yaml_cpp.sh
#      （Windows: install_yaml_cpp.ps1）构建；`lib/` 在 .gitignore 里，所以
#      新 clone 必须跑一次安装脚本，否则这里必然失败。
#   2. 系统安装：发行版 libyaml-cpp-dev / 官方安装包 / CMAKE_PREFIX_PATH。
#
# 项目约定：第三方库随仓库放在 lib/<name>/{include,lib}，由本文件负责产出
# 标准变量，不依赖系统安装。yaml-cpp **首选静态库**，因此使用方必须同时定义
# YAML_CPP_STATIC_DEFINE（见 dll.h），否则头文件会按 __declspec(dllimport)
# 展开，导致 LNK2019。这个宏在非 Windows 平台展开为空，跟着共享库用也无害，
# 所以系统回退路径下一并带上。
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

# ---------------------------------------------------------------------------
# 1) 仓库自带（首选）。变量名带 _vendor 后缀：find_path/find_library 会缓存
#    结果，用不同的变量名才能在"自带没找到"时继续去系统里找。
# ---------------------------------------------------------------------------
find_path(_yaml_cpp_inc_vendor
    NAMES yaml-cpp/yaml.h
    HINTS "${_yaml_cpp_root}/include"
    NO_DEFAULT_PATH
)

find_library(_yaml_cpp_lib_vendor
    NAMES yaml-cpp
    HINTS "${_yaml_cpp_root}/lib"
    NO_DEFAULT_PATH
)

if(_yaml_cpp_inc_vendor AND _yaml_cpp_lib_vendor)
    set(yaml_cpp_INCLUDE_DIR "${_yaml_cpp_inc_vendor}")
    set(yaml_cpp_LIBRARY "${_yaml_cpp_lib_vendor}")
else()
    # -----------------------------------------------------------------------
    # 2) 系统安装（发行版的 libyaml-cpp-dev，或 vcpkg/官方包）。
    #    这是给"跑不了安装脚本"的场景留的兜底（离线、无 git 的构建机）。
    # -----------------------------------------------------------------------
    find_path(yaml_cpp_INCLUDE_DIR NAMES yaml-cpp/yaml.h)
    find_library(yaml_cpp_LIBRARY NAMES yaml-cpp)
    if(yaml_cpp_LIBRARY AND yaml_cpp_INCLUDE_DIR)
        message(STATUS "yaml-cpp: 使用系统安装的版本 (${yaml_cpp_LIBRARY})")
    endif()
endif()

# 先把"怎么修"说清楚：FPHSA 只会打印一行 "missing: yaml_cpp_LIBRARY"，
# 完全指不出是缺依赖，更不会告诉你仓库里本来有安装脚本。
if(NOT yaml_cpp_LIBRARY OR NOT yaml_cpp_INCLUDE_DIR)
    message(STATUS
        "yaml-cpp 未找到（仓库自带位置: ${_yaml_cpp_root}）。修复方式任选其一：\n"
        "    Linux/macOS : bash scripts/install_yaml_cpp.sh\n"
        "    Windows     : powershell -ExecutionPolicy Bypass -File scripts/install_yaml_cpp.ps1\n"
        "    发行版包    : sudo apt install libyaml-cpp-dev\n"
        "  说明：lib/ 目录不入库，新 clone 的仓库必须跑一次安装脚本。")
endif()

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
