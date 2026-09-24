# Locate LibRaw without relying on a package-provided CMake config. Some LibRaw
# installations ship configs containing machine-specific paths, while pkg-config
# and the conventional include/library layout remain relocatable.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LibRaw QUIET libraw)
endif()

find_path(LibRaw_INCLUDE_DIR
    NAMES libraw/libraw.h
    HINTS ${PC_LibRaw_INCLUDE_DIRS}
)

set(LibRaw_VERSION "${PC_LibRaw_VERSION}")
if(NOT LibRaw_VERSION AND LibRaw_INCLUDE_DIR AND
   EXISTS "${LibRaw_INCLUDE_DIR}/libraw/libraw_version.h")
    file(STRINGS "${LibRaw_INCLUDE_DIR}/libraw/libraw_version.h" version_defines
        REGEX "^#define LIBRAW_(MAJOR|MINOR|PATCH)_VERSION [0-9]+$")
    foreach(component IN ITEMS MAJOR MINOR PATCH)
        string(REGEX MATCH "#define LIBRAW_${component}_VERSION ([0-9]+)"
            version_match "${version_defines}")
        set(_libraw_${component} "${CMAKE_MATCH_1}")
    endforeach()
    if(NOT "${_libraw_MAJOR}" STREQUAL "" AND NOT "${_libraw_MINOR}" STREQUAL "" AND
       NOT "${_libraw_PATCH}" STREQUAL "")
        set(LibRaw_VERSION "${_libraw_MAJOR}.${_libraw_MINOR}.${_libraw_PATCH}")
    endif()
endif()

# Prefer the reentrant variant when a distribution provides both libraries.
find_library(LibRaw_REENTRANT_LIBRARY
    NAMES raw_r libraw_r
    HINTS ${PC_LibRaw_LIBRARY_DIRS}
)
find_library(LibRaw_STANDARD_LIBRARY
    NAMES raw libraw
    HINTS ${PC_LibRaw_LIBRARY_DIRS}
)

if(LibRaw_REENTRANT_LIBRARY)
    set(LibRaw_LIBRARY "${LibRaw_REENTRANT_LIBRARY}")
    set(LibRaw_IS_REENTRANT TRUE)
else()
    set(LibRaw_LIBRARY "${LibRaw_STANDARD_LIBRARY}")
    set(LibRaw_IS_REENTRANT FALSE)
endif()
get_filename_component(LibRaw_LIBRARY_DIR "${LibRaw_LIBRARY}" DIRECTORY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibRaw
    REQUIRED_VARS LibRaw_INCLUDE_DIR LibRaw_LIBRARY
    VERSION_VAR LibRaw_VERSION
)

if(LibRaw_FOUND AND NOT TARGET LibRaw::LibRaw)
    add_library(LibRaw::LibRaw UNKNOWN IMPORTED)
    set_target_properties(LibRaw::LibRaw PROPERTIES
        IMPORTED_LOCATION "${LibRaw_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LibRaw_INCLUDE_DIR};${LibRaw_INCLUDE_DIR}/libraw"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
            "${LibRaw_INCLUDE_DIR};${LibRaw_INCLUDE_DIR}/libraw"
    )
endif()

mark_as_advanced(
    LibRaw_INCLUDE_DIR
    LibRaw_LIBRARY
    LibRaw_LIBRARY_DIR
    LibRaw_REENTRANT_LIBRARY
    LibRaw_STANDARD_LIBRARY
)
