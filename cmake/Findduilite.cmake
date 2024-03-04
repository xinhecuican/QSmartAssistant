find_package(PkgConfig)
set(LIB_NAME duilite)
set(${LIB_NAME}_ROOT_DIR ${PROJECT_SOURCE_DIR}/lib/${LIB_NAME})
find_path(${LIB_NAME}_INCLUDE_DIR duilite.h ${${LIB_NAME}_ROOT_DIR}/include)

macro(_FIND_LIBRARY libname)
    if(NOT ${LIB_NAME}_${libname}_LIBRARY)
        find_library(${LIB_NAME}_${libname}_LIBRARY 
        NAMES ${libname} 
        HINTS ${duilite_ROOT_DIR}/lib)
        list(APPEND ${LIB_NAME}_LIBRARY ${${LIB_NAME}_${libname}_LIBRARY})
    endif()
endmacro(_FIND_LIBRARY)

_FIND_LIBRARY(auth_linux)
_FIND_LIBRARY(upload_linux)

if(${LIB_NAME}_LIBRARY)
    set(${LIB_NAME}_FOUND TRUE CACHE BOOL "")
    set(${LIB_NAME}_INCLUDE_DIRS ${${LIB_NAME}_INCLUDE_DIR} CACHE STRING "")
    set(${LIB_NAME}_LIBRARIES ${${LIB_NAME}_LIBRARY} CACHE STRING "")
    set(CMAKE_REQUIRED_INCLUDES ${${LIB_NAME}_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES ${${LIB_NAME}_LIBRARIES})
    find_package_handle_standard_args(${LIB_NAME}
    REQUIRED_VARS ${LIB_NAME}_INCLUDE_DIRS ${LIB_NAME}_LIBRARIES
    VERSION_VAR 1.0)
    mark_as_advanced(${LIB_NAME}_INCLUDE_DIRS ${LIB_NAME}_LIBRARIES)
endif()
if(${LIB_NAME}_FOUND)
    if(NOT TARGET ${LIB_NAME})
        add_library(${LIB_NAME} SHARED IMPORTED )
        set_target_properties(${LIB_NAME} PROPERTIES 
            INTERFACE_INCLUDE_DIRECTORIES "${${LIB_NAME}_INCLUDE_DIRS}"
            IMPORTED_IMPLIB "${${LIB_NAME}_LIBRARIES}"
            IMPORTED_NO_SONAME true
            IMPORTED_LOCATION "${${LIB_NAME}_LIBRARIES}")
    endif()
endif()
