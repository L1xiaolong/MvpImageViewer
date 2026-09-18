qt_add_executable(ispview_core_tests
    core_tests.cpp
)
target_link_libraries(ispview_core_tests PRIVATE ispview_core Qt6::Test)
ispview_enable_warnings(ispview_core_tests)
add_test(NAME ispview_core_tests COMMAND ispview_core_tests)

qt_add_executable(ispview_io_tests
    io_tests.cpp
)
target_link_libraries(ispview_io_tests PRIVATE ispview_io Qt6::Test)
target_compile_definitions(ispview_io_tests PRIVATE
    ISPVIEW_HAS_LIBRAW=$<BOOL:${ISPVIEW_HAS_LIBRAW}>
    ISPVIEW_HAS_EXIV2=$<BOOL:${ISPVIEW_HAS_EXIV2}>
    ISPVIEW_HAS_LCMS2=$<BOOL:${ISPVIEW_HAS_LCMS2}>
)
if(ISPVIEW_HAS_EXIV2)
    target_link_libraries(ispview_io_tests PRIVATE Exiv2::exiv2lib)
endif()
ispview_enable_warnings(ispview_io_tests)
add_test(NAME ispview_io_tests COMMAND ispview_io_tests)

qt_add_executable(ispview_qml_workspace_tests
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
target_link_libraries(ispview_qml_workspace_tests PRIVATE
    ispview_browser_backend ispview_render_backend ispview_io
    Qt6::Quick Qt6::GuiPrivate Qt6::Network Qt6::Test
)
target_compile_definitions(ispview_qml_workspace_tests PRIVATE
    ISPVIEW_HAS_LIBRAW=$<BOOL:${ISPVIEW_HAS_LIBRAW}>
)
ispview_enable_warnings(ispview_qml_workspace_tests)
add_test(NAME ispview_qml_workspace_tests COMMAND ispview_qml_workspace_tests)
set_tests_properties(ispview_qml_workspace_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(ispview_qml_interaction_tests
    qml_test_main.cpp
)
target_link_libraries(ispview_qml_interaction_tests PRIVATE Qt6::QuickTest)
target_compile_definitions(ispview_qml_interaction_tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
ispview_enable_warnings(ispview_qml_interaction_tests)
add_test(NAME ispview_qml_interaction_tests COMMAND ispview_qml_interaction_tests)
set_tests_properties(ispview_qml_interaction_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;QML_DISABLE_DISK_CACHE=1"
)

qt_add_executable(ispview_render_tests
    render_tests.cpp
)
target_link_libraries(ispview_render_tests PRIVATE ispview_io ispview_render_backend Qt6::Test)
ispview_enable_warnings(ispview_render_tests)
add_test(NAME ispview_render_tests COMMAND ispview_render_tests)

qt_add_executable(ispview_diagnostics_crash_fixture diagnostics_crash_fixture.cpp)
target_link_libraries(ispview_diagnostics_crash_fixture PRIVATE ispview_diagnostics)
qt_add_executable(ispview_diagnostics_tests diagnostics_tests.cpp)
target_link_libraries(ispview_diagnostics_tests PRIVATE ispview_diagnostics ispview_miniz Qt6::Test)
target_compile_definitions(ispview_diagnostics_tests PRIVATE
    ISPVIEW_HAS_CRASHPAD=$<BOOL:${ISPVIEW_HAS_CRASHPAD}>)
if(WIN32)
    add_dependencies(ispview_diagnostics_crash_fixture ispview_crash_handler)
    add_custom_command(TARGET ispview_diagnostics_crash_fixture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:ispview_crash_handler>
            $<TARGET_FILE_DIR:ispview_diagnostics_crash_fixture>)
endif()
if(APPLE AND ISPVIEW_HAS_CRASHPAD)
    add_custom_command(TARGET ispview_diagnostics_crash_fixture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ispview_diagnostics_crash_fixture>/../Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ISPVIEW_CRASHPAD_HANDLER}"
            "$<TARGET_FILE_DIR:ispview_diagnostics_crash_fixture>/../Helpers/crashpad_handler")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME ispview_diagnostics_tests COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/tests/run_diagnostics_tests.py" $<TARGET_FILE:ispview_diagnostics_tests>)
set_tests_properties(ispview_diagnostics_tests PROPERTIES TIMEOUT 180)
