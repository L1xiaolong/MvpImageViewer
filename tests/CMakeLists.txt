add_executable(yuv_startup_regression yuv_startup_regression.cpp)
target_link_libraries(yuv_startup_regression PRIVATE mvpview_io)
mvpview_enable_warnings(yuv_startup_regression)
add_test(NAME yuv_startup_regression COMMAND yuv_startup_regression)
set_tests_properties(yuv_startup_regression PROPERTIES TIMEOUT 60)

add_executable(rhi_fallback_regression rhi_fallback_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/qml_image_canvas.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/qml_image_canvas.h)
target_link_libraries(rhi_fallback_regression PRIVATE
    mvpview_render_backend mvpview_diagnostics Qt6::Quick Qt6::GuiPrivate)
mvpview_enable_warnings(rhi_fallback_regression)
add_test(NAME rhi_fallback_regression COMMAND rhi_fallback_regression)
set_tests_properties(rhi_fallback_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=rhi")

if(APPLE)
    add_executable(canvas_pixels_regression canvas_pixels_regression.cpp
        ${PROJECT_SOURCE_DIR}/src/qml/qml_image_canvas.cpp
        ${PROJECT_SOURCE_DIR}/src/qml/qml_image_canvas.h)
    target_link_libraries(canvas_pixels_regression PRIVATE
        mvpview_io mvpview_render_backend mvpview_diagnostics Qt6::Quick Qt6::GuiPrivate)
    mvpview_enable_warnings(canvas_pixels_regression)
    add_test(NAME canvas_pixels_regression COMMAND canvas_pixels_regression)
    set_tests_properties(canvas_pixels_regression PROPERTIES TIMEOUT 60)
endif()

add_executable(browse_restore_regression browse_restore_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/browse_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/thumbnail_image_provider.cpp)
target_link_libraries(browse_restore_regression PRIVATE
    mvpview_browser_backend mvpview_platform mvpview_io Qt6::Quick)
mvpview_enable_warnings(browse_restore_regression)
add_test(NAME browse_restore_regression COMMAND browse_restore_regression)
set_tests_properties(browse_restore_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(thumbnail_response_regression thumbnail_response_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/thumbnail_image_provider.cpp)
target_link_libraries(thumbnail_response_regression PRIVATE mvpview_io Qt6::Quick)
mvpview_enable_warnings(thumbnail_response_regression)
add_test(NAME thumbnail_response_regression COMMAND thumbnail_response_regression)
set_tests_properties(thumbnail_response_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Exercise the real settings QML and AppSettings with a temporary QSettings store.
find_package(Qt6 6.9 REQUIRED COMPONENTS QuickTest)
add_executable(settings_ui_regression settings_ui_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/app_settings.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/app_settings.h)
target_link_libraries(settings_ui_regression PRIVATE
    mvpview_io Qt6::QuickTest Qt6::QuickControls2 Qt6::Network)
target_compile_definitions(settings_ui_regression PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    SETTINGS_ARTIFACT_DIR="${PROJECT_BINARY_DIR}/settings-ui"
    MVPVIEW_GITHUB_REPOSITORY="${MVPVIEW_GITHUB_REPOSITORY}")
foreach(_variant ui ui-dark)
    set(_settings_icons)
    foreach(_icon chevron-down)
        list(APPEND _settings_icons "${PROJECT_SOURCE_DIR}/assets/icons/${_variant}/${_icon}.svg")
    endforeach()
    qt_add_resources(settings_ui_regression "settings_test_${_variant}"
        PREFIX "/icons/${_variant}" BASE "${PROJECT_SOURCE_DIR}/assets/icons/${_variant}" FILES ${_settings_icons})
endforeach()
qt_add_resources(settings_ui_regression settings_test_translations
    PREFIX "/i18n" BASE "${PROJECT_BINARY_DIR}/src/qml"
    FILES "${PROJECT_BINARY_DIR}/src/qml/mvpimageviewer_zh_CN.qm")
add_dependencies(settings_ui_regression MVPImageViewerQml)
add_test(NAME settings_ui_regression COMMAND settings_ui_regression)
set_tests_properties(settings_ui_regression PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

add_executable(properties_ui_regression properties_ui_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/app_settings.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/app_settings.h)
target_link_libraries(properties_ui_regression PRIVATE mvpview_io Qt6::QuickTest Qt6::QuickControls2 Qt6::Network)
target_compile_definitions(properties_ui_regression PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/properties-qml"
    PROPERTIES_ARTIFACT_DIR="${PROJECT_BINARY_DIR}/properties-ui"
    MVPVIEW_GITHUB_REPOSITORY="${MVPVIEW_GITHUB_REPOSITORY}")
foreach(_variant ui ui-dark)
    qt_add_resources(properties_ui_regression "properties_test_${_variant}"
        PREFIX "/icons/${_variant}" BASE "${PROJECT_SOURCE_DIR}/assets/icons/${_variant}"
        FILES "${PROJECT_SOURCE_DIR}/assets/icons/${_variant}/chevron-down.svg")
endforeach()
qt_add_resources(properties_ui_regression properties_test_translations
    PREFIX "/i18n" BASE "${PROJECT_BINARY_DIR}/src/qml"
    FILES "${PROJECT_BINARY_DIR}/src/qml/mvpimageviewer_zh_CN.qm")
add_dependencies(properties_ui_regression MVPImageViewerQml)
add_test(NAME properties_ui_regression COMMAND properties_ui_regression -o -,txt)
set_tests_properties(properties_ui_regression PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
add_test(NAME properties_ui_hidpi_regression COMMAND properties_ui_regression PropertiesUI::test_size_and_keyboard -o -,txt)
set_tests_properties(properties_ui_hidpi_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=2")

add_executable(properties_controller_regression properties_controller_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/image_properties_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/image_properties_controller.h)
target_link_libraries(properties_controller_regression PRIVATE mvpview_io Qt6::Gui)
mvpview_enable_warnings(properties_controller_regression)
add_test(NAME properties_controller_regression COMMAND properties_controller_regression)
set_tests_properties(properties_controller_regression PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(raw_parameters_controller_regression raw_parameters_controller_regression.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/raw_parameters_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/qml/raw_parameters_controller.h)
target_link_libraries(raw_parameters_controller_regression PRIVATE mvpview_io Qt6::Gui)
mvpview_enable_warnings(raw_parameters_controller_regression)
add_test(NAME raw_parameters_controller_regression COMMAND raw_parameters_controller_regression)
set_tests_properties(raw_parameters_controller_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME raw_parameters_ui_hidpi_regression COMMAND properties_ui_regression PropertiesUI::test_raw_size_and_keyboard -o -,txt)
set_tests_properties(raw_parameters_ui_hidpi_regression PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=2")
