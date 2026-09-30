qt_add_executable(mvpview_core_tests
    core_tests.cpp
)
target_link_libraries(mvpview_core_tests PRIVATE mvpview_core Qt6::Test)
mvpview_enable_warnings(mvpview_core_tests)
add_test(NAME mvpview_core_tests COMMAND mvpview_core_tests)

qt_add_executable(mvpview_io_tests
    io_tests.cpp
)
target_link_libraries(mvpview_io_tests PRIVATE mvpview_io Qt6::Test)
target_compile_definitions(mvpview_io_tests PRIVATE
    MVPVIEW_HAS_LIBRAW=$<BOOL:${MVPVIEW_HAS_LIBRAW}>
    MVPVIEW_HAS_EXIV2=$<BOOL:${MVPVIEW_HAS_EXIV2}>
    MVPVIEW_HAS_LCMS2=$<BOOL:${MVPVIEW_HAS_LCMS2}>
)
if(MVPVIEW_HAS_EXIV2)
    target_link_libraries(mvpview_io_tests PRIVATE Exiv2::exiv2lib)
endif()
mvpview_enable_warnings(mvpview_io_tests)
if(WIN32)
    # QtTest's text logger is not forwarded through CTest's captured stdout on MSYS2.
    # Keep a dedicated report so the release gate can expose the actual failing assertion.
    add_test(NAME mvpview_io_tests COMMAND mvpview_io_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/mvpview_io_tests.log,txt")
else()
    add_test(NAME mvpview_io_tests COMMAND mvpview_io_tests)
endif()

qt_add_executable(mvpview_qml_workspace_tests
    qml_workspace_controller_tests.cpp
    ../src/qml/app_settings.h
    ../src/qml/app_settings.cpp
    ../src/qml/browse_controller.h
    ../src/qml/browse_controller.cpp
    ../src/qml/browse_workspace_controller.h
    ../src/qml/browse_workspace_controller.cpp
    ../src/qml/compare_controller.h
    ../src/qml/compare_controller.cpp
    ../src/qml/image_properties_controller.h
    ../src/qml/image_properties_controller.cpp
    ../src/qml/full_screen_controller.h
    ../src/qml/full_screen_controller.cpp
    ../src/qml/raw_parameters_controller.h
    ../src/qml/raw_parameters_controller.cpp
    ../src/qml/qml_image_canvas.h
    ../src/qml/qml_image_canvas.cpp
    ../src/qml/thumbnail_image_provider.h
    ../src/qml/thumbnail_image_provider.cpp
)
target_link_libraries(mvpview_qml_workspace_tests PRIVATE
    mvpview_browser_backend mvpview_render_backend mvpview_io
    Qt6::Quick Qt6::GuiPrivate Qt6::Network Qt6::Test
)
target_compile_definitions(mvpview_qml_workspace_tests PRIVATE
    MVPVIEW_HAS_LIBRAW=$<BOOL:${MVPVIEW_HAS_LIBRAW}>
    MVPVIEW_GITHUB_REPOSITORY=""
)
mvpview_enable_warnings(mvpview_qml_workspace_tests)
add_test(NAME mvpview_qml_workspace_tests COMMAND mvpview_qml_workspace_tests)
set_tests_properties(mvpview_qml_workspace_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(mvpview_qml_interaction_tests
    qml_test_main.cpp
)
target_link_libraries(mvpview_qml_interaction_tests PRIVATE Qt6::QuickTest)
target_compile_definitions(mvpview_qml_interaction_tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
mvpview_enable_warnings(mvpview_qml_interaction_tests)
add_test(NAME mvpview_qml_interaction_tests COMMAND mvpview_qml_interaction_tests)
set_tests_properties(mvpview_qml_interaction_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QML_DISABLE_DISK_CACHE=1"
)

qt_add_executable(mvpview_render_tests
    render_tests.cpp
)
target_link_libraries(mvpview_render_tests PRIVATE mvpview_io mvpview_render_backend Qt6::Test)
mvpview_enable_warnings(mvpview_render_tests)
add_test(NAME mvpview_render_tests COMMAND mvpview_render_tests)

qt_add_executable(mvpview_diagnostics_crash_fixture diagnostics_crash_fixture.cpp)
target_link_libraries(mvpview_diagnostics_crash_fixture PRIVATE mvpview_diagnostics)
qt_add_executable(mvpview_diagnostics_tests diagnostics_tests.cpp)
target_link_libraries(mvpview_diagnostics_tests PRIVATE mvpview_diagnostics mvpview_miniz Qt6::Test)
target_compile_definitions(mvpview_diagnostics_tests PRIVATE
    MVPVIEW_HAS_CRASHPAD=$<BOOL:${MVPVIEW_HAS_CRASHPAD}>)
if(WIN32)
    add_dependencies(mvpview_diagnostics_crash_fixture mvpview_crash_handler)
    add_custom_command(TARGET mvpview_diagnostics_crash_fixture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:mvpview_crash_handler>
            $<TARGET_FILE_DIR:mvpview_diagnostics_crash_fixture>)
endif()
if(APPLE AND MVPVIEW_HAS_CRASHPAD)
    add_custom_command(TARGET mvpview_diagnostics_crash_fixture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mvpview_diagnostics_crash_fixture>/../Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MVPVIEW_CRASHPAD_HANDLER}"
            "$<TARGET_FILE_DIR:mvpview_diagnostics_crash_fixture>/../Helpers/crashpad_handler")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME mvpview_diagnostics_tests COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/tests/run_diagnostics_tests.py" $<TARGET_FILE:mvpview_diagnostics_tests>)
set_tests_properties(mvpview_diagnostics_tests PROPERTIES TIMEOUT 180)
