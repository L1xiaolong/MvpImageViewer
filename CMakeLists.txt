cmake_minimum_required(VERSION 3.25)

project(ISPImageViewer VERSION 0.2.11 LANGUAGES CXX)
enable_language(C)

option(ISPVIEW_BUILD_BENCHMARKS "Build repeatable local performance tools" OFF)
option(ISPVIEW_ENABLE_LIBRAW "Enable camera RAW decoding when LibRaw is available" ON)
option(ISPVIEW_ENABLE_EXIV2 "Enable EXIF/IPTC/XMP metadata when Exiv2 is available" ON)
option(ISPVIEW_ENABLE_LCMS2 "Enable embedded ICC conversion with LittleCMS when available" ON)
option(ISPVIEW_ENABLE_CRASHPAD "Enable macOS Crashpad crash capture when the pinned build succeeds" ON)
set(ISPVIEW_GITHUB_REPOSITORY "" CACHE STRING
    "GitHub owner/repository used for update checks and project links")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(ISPVIEW_HAS_LIBRAW OFF)
if(ISPVIEW_ENABLE_LIBRAW)
    find_package(LibRaw QUIET)
    if(LibRaw_FOUND)
        set(_ispview_libraw_usable TRUE)
        if(APPLE AND NOT CMAKE_CROSSCOMPILING)
            include(CheckCXXSourceRuns)
            set(CMAKE_REQUIRED_INCLUDES "${LibRaw_INCLUDE_DIR}/libraw")
            set(CMAKE_REQUIRED_LIBRARIES "${LibRaw_LIBRARY}")
            check_cxx_source_runs(
                "#include <libraw.h>\nint main() { return libraw_version() ? 0 : 1; }"
                ISPVIEW_LIBRAW_RUNTIME_WORKS
            )
            unset(CMAKE_REQUIRED_INCLUDES)
            unset(CMAKE_REQUIRED_LIBRARIES)
            if(NOT ISPVIEW_LIBRAW_RUNTIME_WORKS)
                set(_ispview_libraw_usable FALSE)
                message(WARNING
                    "LibRaw was found but its runtime dependencies are unusable; "
                    "camera RAW support is disabled for this build")
            endif()
        endif()
        if(_ispview_libraw_usable)
            set(ISPVIEW_HAS_LIBRAW ON)
            if(APPLE)
                # Locally discovered LibRaw builds commonly use an @rpath install name.
                # Derive the build-tree search path from discovery; packaging will copy
                # dependencies and use application-relative paths in a later phase.
                list(APPEND CMAKE_BUILD_RPATH "${LibRaw_LIBRARY_DIR}")
            endif()
            message(STATUS "Camera RAW support: LibRaw ${PC_LibRaw_VERSION}")
        endif()
    else()
        message(STATUS "Camera RAW support disabled: LibRaw was not found")
    endif()
endif()

set(ISPVIEW_HAS_EXIV2 OFF)
if(ISPVIEW_ENABLE_EXIV2)
    find_package(exiv2 CONFIG QUIET)
    if(TARGET Exiv2::exiv2lib)
        set(ISPVIEW_HAS_EXIV2 ON)
        message(STATUS "Image metadata support: Exiv2 enabled")
    else()
        message(STATUS "Image metadata support disabled: Exiv2 was not found")
    endif()
endif()

set(ISPVIEW_HAS_LCMS2 OFF)
if(ISPVIEW_ENABLE_LCMS2)
    find_package(LCMS2 QUIET)
    if(LCMS2_FOUND)
        set(ISPVIEW_HAS_LCMS2 ON)
        message(STATUS "Embedded ICC color management: LittleCMS ${LCMS2_VERSION}")
    else()
        message(STATUS "Embedded ICC color management disabled: LittleCMS was not found")
    endif()
endif()

# macOS Crashpad is built by its own GN pipeline (cmake/Crashpad.cmake) when the diagnostics
# target is configured. Switching it off keeps the rest of the feature intact: the service
# reports crash capture as unavailable instead of failing the configuration.
set(ISPVIEW_HAS_CRASHPAD OFF)
if(APPLE AND ISPVIEW_ENABLE_CRASHPAD)
    set(ISPVIEW_HAS_CRASHPAD ON)
    message(STATUS "Crash capture: Crashpad (macOS)")
elseif(APPLE)
    message(STATUS "Crash capture disabled: macOS Crashpad is switched off")
endif()

# Qt 6.9 is the validated baseline. The QML module policies used below (QTP0004, Qt 6.8+), the
# QQuickRhiItem compositing behaviour the canvas relies on, and the Qt6::GuiPrivate dependency
# all require a matched Qt build, so the minimum and the packaged runtime must not diverge.
find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core Gui Network Quick QuickControls2 QuickLayouts QuickTemplates2 Svg ShaderTools LinguistTools
)
find_package(Qt6GuiPrivate 6.9 CONFIG REQUIRED)

# The production QML renderer uses Qt's QRhi API.  Keep this check close to
# dependency discovery so an incomplete Qt SDK fails during configuration with
# an actionable error instead of much later while compiling qml_image_canvas.
if(NOT TARGET Qt6::GuiPrivate)
    message(FATAL_ERROR
        "Qt6::GuiPrivate is required by the QML RHI renderer. Install the "
        "private QtGui development headers for the selected Qt SDK.")
endif()

if(BUILD_TESTING OR ISPVIEW_BUILD_BENCHMARKS)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
endif()
if(BUILD_TESTING)
    find_package(Qt6 6.9 REQUIRED COMPONENTS QuickTest)
endif()

# macOS 26 keeps the legacy AGL framework directory but removes its binary. Qt 6.9's
# WrapOpenGL probe sees the directory and adds an unusable link flag even though this app uses
# Metal. Keep OpenGL (required by QtGui's interface) while dropping only the missing AGL link.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL
   AND NOT EXISTS "/System/Library/Frameworks/AGL.framework/Versions/A/AGL")
    set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY
        INTERFACE_LINK_LIBRARIES "-framework OpenGL")
endif()

qt_standard_project_setup()
include(CTest)
include(cmake/Warnings.cmake)

execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE ISPVIEW_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT ISPVIEW_BUILD_ID)
    set(ISPVIEW_BUILD_ID "source-archive")
endif()
# Keep release optimization while retaining line tables for matching diagnostic symbols.
if(MSVC)
    add_compile_options($<$<NOT:$<CONFIG:Debug>>:/Zi>)
else()
    add_compile_options(-g)
endif()
add_subdirectory(third_party/miniz)
add_subdirectory(src)

if(ISPVIEW_BUILD_BENCHMARKS)
    add_subdirectory(tools)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
