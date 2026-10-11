# CopyDeps.cmake - 将第三方依赖的运行库复制到目标目录（Windows 用）
#
# 用法:
#   cmake -DOUTPUT_DIR=<目标目录> "-DDEP_DIRS=<目录1>;<目录2>;..." -P CopyDeps.cmake
#
# 后出现的目录中的同名文件会覆盖先出现的（例如 sherpa-onnx 自带的
# onnxruntime.dll 会覆盖单独下载的版本）。

if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "CopyDeps.cmake: 未指定 OUTPUT_DIR")
endif()

if(NOT IS_DIRECTORY "${OUTPUT_DIR}")
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
endif()

set(_patterns "*.dll" "*.dylib" "*.so")

foreach(dir IN LISTS DEP_DIRS)
    if(IS_DIRECTORY "${dir}")
        foreach(_pat IN LISTS _patterns)
            file(GLOB _files "${dir}/${_pat}")
            foreach(_f IN LISTS _files)
                get_filename_component(_name "${_f}" NAME)
                execute_process(
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                            "${_f}" "${OUTPUT_DIR}/${_name}")
                message(STATUS "  [CopyDeps] ${_name}")
            endforeach()
        endforeach()
    else()
        message(STATUS "  [CopyDeps] 跳过不存在的目录: ${dir}")
    endif()
endforeach()
