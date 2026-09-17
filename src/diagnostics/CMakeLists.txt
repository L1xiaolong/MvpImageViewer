qt_add_library(ispview_diagnostics STATIC
    diagnostics.h diagnostics.cpp crash_capture.h crash_capture.cpp
    diagnostics_controller.h diagnostics_controller.cpp)
target_include_directories(ispview_diagnostics PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ispview_diagnostics PUBLIC Qt6::Core Qt6::Gui PRIVATE ispview_miniz)
target_compile_definitions(ispview_diagnostics PRIVATE
    ISPVIEW_PROJECT_VERSION="${PROJECT_VERSION}" ISPVIEW_BUILD_ID="${ISPVIEW_BUILD_ID}"
    ISPVIEW_HAS_CRASHPAD=$<BOOL:${ISPVIEW_HAS_CRASHPAD}>)
ispview_enable_warnings(ispview_diagnostics)
if(WIN32)
    add_executable(ispview_crash_handler WIN32 crash_handler_win.cpp)
    target_link_libraries(ispview_crash_handler PRIVATE dbghelp shell32)
    if(MINGW)
        target_link_options(ispview_crash_handler PRIVATE -municode -static-libgcc -static-libstdc++)
    endif()
endif()
if(APPLE AND ISPVIEW_HAS_CRASHPAD)
    include(${PROJECT_SOURCE_DIR}/cmake/Crashpad.cmake)
    target_link_libraries(ispview_diagnostics PRIVATE ispview_crashpad)
endif()
