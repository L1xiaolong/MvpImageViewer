qt_add_library(mvpview_diagnostics STATIC
    diagnostics.h diagnostics.cpp crash_capture.h crash_capture.cpp
    diagnostics_controller.h diagnostics_controller.cpp)
target_include_directories(mvpview_diagnostics PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mvpview_diagnostics PUBLIC Qt6::Core Qt6::Gui PRIVATE mvpview_miniz)
target_compile_definitions(mvpview_diagnostics PRIVATE
    MVPVIEW_PROJECT_VERSION="${PROJECT_VERSION}" MVPVIEW_BUILD_ID="${MVPVIEW_BUILD_ID}"
    MVPVIEW_HAS_CRASHPAD=$<BOOL:${MVPVIEW_HAS_CRASHPAD}>)
mvpview_enable_warnings(mvpview_diagnostics)
if(WIN32)
    add_executable(mvpview_crash_handler WIN32 crash_handler_win.cpp)
    target_link_libraries(mvpview_crash_handler PRIVATE dbghelp shell32)
    if(MINGW)
        target_link_options(mvpview_crash_handler PRIVATE -municode -static-libgcc -static-libstdc++)
    endif()
endif()
if(APPLE AND MVPVIEW_HAS_CRASHPAD)
    include(${PROJECT_SOURCE_DIR}/cmake/Crashpad.cmake)
    target_link_libraries(mvpview_diagnostics PRIVATE mvpview_crashpad)
endif()
